#include "ext2_fs.h"
#include "read_ext2.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#define ERR_FILE_EMPTY 1
#define ERR_NOT_JPEG 2
#define ERR_MALLOC 3
#define TOP_SECRET_DIR "top_secret"

const char INODE_PREFIX[] = "file-";
const char JPEG_EXT[] = ".jpg";
const char DETAILS_EXT[] = "-details.txt";

int is_jpeg(char* buffer) {
    if (
        buffer[0] == (char)0xff &&
        buffer[1] == (char)0xd8 &&
        buffer[2] == (char)0xff &&
        (buffer[3] == (char)0xe0 ||
            buffer[3] == (char)0xe1 ||
            buffer[3] == (char)0xe8)) {
        return 1;
    }
    return 0;
}

void read_data_block(int fd, __u32 block_num, char** write_inc, char* head, __u32 file_size) {
    lseek(fd, BLOCK_OFFSET(block_num), SEEK_SET);

    __u32 bytes_remaining = file_size - (*write_inc - head);

    if (bytes_remaining < block_size) {
        read(fd, *write_inc, bytes_remaining);
        *write_inc += bytes_remaining;
    }
    else {

        read(fd, *write_inc, block_size);
        *write_inc += block_size;
    }
}

void read_indirect_block(int fd, __u32 ind_blk_ptr, char** write_inc, char* head, __u32 file_size) {
    // read indirect block at node->i_block[i] => list of block pointers
    // loop through list of block pointers with index j
    // read blocks at block pointers[j] => block data

    // list of data block pointers
    __u32 blocks[block_size / sizeof(__u32)];

    lseek(fd, BLOCK_OFFSET(ind_blk_ptr), SEEK_SET);
    read(fd, blocks, block_size);

    for (int j = 0; j < 256; j++) {
        if (blocks[j] == 0) {
            continue;
        }
        read_data_block(fd, blocks[j], write_inc, head, file_size);
    }
}

void output_details_file(struct ext2_inode* inode, char* filename) {
    FILE* f = fopen(filename, "w");
    fprintf(f, "%u\n%u\n%u", inode->i_links_count, inode->i_size, inode->i_uid);
    fflush(f);
    fclose(f);
}

int output_image(int fd, struct ext2_inode* inode, __u32 inode_num, char* output_dir, char* filename) {
    if (inode->i_size == 0) {
        return ERR_FILE_EMPTY;
    }
    // allocate buffer for inode->i_size
    __u32 file_size = inode->i_size;
    char* data = calloc(1, file_size);
    if (data == NULL)
        return ERR_MALLOC;
    char* write_inc = data;

    // iterate through inode block pointers to read data blocks from fd
    // block pointers: 0-11: direct data blocks, 12: indirect, 13: double indirect, 14: triple indirect
    // for this project, only first and second indirect pointers used
    for (int i = 0; i < 14; i++) {
        if (inode->i_block[i] == 0) {
            write_inc += block_size;
            continue;
        };
        if (i < 12) { // direct data block
            read_data_block(fd, inode->i_block[i], &write_inc, data, file_size);

            if (i == 0 && !is_jpeg(data)) {
                printf("not a jpeg!\n");
                free(data);
                return ERR_NOT_JPEG;
            }
        }
        else if (i == 12) { // indirect block
            // inode->i_block[i]: indirect block pointer
            read_indirect_block(fd, inode->i_block[i], &write_inc, data, file_size);
        }
        else if (i == 13) {                                             // double indirect
            __u32 blocks[block_size / sizeof(__u32)]; // array of indirect pointers

            lseek(fd, BLOCK_OFFSET(inode->i_block[i]), SEEK_SET);
            read(fd, blocks, block_size);

            for (int j = 0; j < 256; j++) {
                if (blocks[j] == 0) {
                    continue;
                }
                read_indirect_block(fd, blocks[j], &write_inc, data, file_size);
            }
        }
    }

    char output_filename[strlen(output_dir) + 1 + strlen(filename)];
    char inode_filename[strlen(output_dir) + 1 + sizeof(INODE_PREFIX) + 4 + sizeof(JPEG_EXT)];
    char details_filename[strlen(output_dir) + 1 + sizeof(INODE_PREFIX) + 4 + sizeof(DETAILS_EXT)];

    sprintf(output_filename, "%s/%s", output_dir, filename);
    sprintf(inode_filename, "%s/file-%u.jpg", output_dir, inode_num);
    sprintf(details_filename, "%s/file-%u-details.txt", output_dir, inode_num);

    // write to output file
    int ofile = open(output_filename, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    write(ofile, data, file_size);
    close(ofile);

    // write to file-[inum].jpg file
    int ifile = open(inode_filename, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    write(ifile, data, file_size);
    close(ifile);

    // write details file
    output_details_file(inode, details_filename);

    free(data);
    return 0;
}

/** returns offset of next directory */
off_t read_directory(int fd,
    __u32 block,
    off_t offset,
    struct ext2_dir_entry* dir_entry,
    struct ext2_super_block* super) {
    lseek(fd, offset + BLOCK_OFFSET(block), SEEK_SET);
    char buf[sizeof(struct ext2_dir_entry)];
    read(fd, buf, sizeof(struct ext2_dir_entry));
    struct ext2_dir_entry* dent = (struct ext2_dir_entry*)(&buf);

    if (dent->inode > super->s_inodes_count + EXT2_ROOT_INO || dent->inode < EXT2_ROOT_INO) {
        return -1;
    }

    int name_len = dent->name_len & 0xFF;
    dir_entry->inode = dent->inode;
    dir_entry->rec_len = dent->rec_len;
    dir_entry->name_len = name_len;
    dir_entry->file_type = dent->file_type;
    strncpy(dir_entry->name, dent->name, name_len);
    dir_entry->name[name_len] = '\0';

    off_t next_dentry = offset + sizeof(struct ext2_dir_entry_2) + name_len; // calculate true size of dentry
    return (next_dentry + 3) & ~3;                                           // align to 4 bytes (rounded up)
}

int traverse_directory(struct ext2_inode* itable,
    int fd,
    struct ext2_inode* inode,
    char* output_dir,
    struct ext2_super_block* super) {

    off_t offset = 0;
    struct ext2_dir_entry dentry;
    while (offset < block_size) {
        offset = read_directory(fd, inode->i_block[0], offset, &dentry, super);
        if (offset < 0) {
            break;
        }

        __u32 i = dentry.inode;

        // if "top_secret" directory found, dump its contents and exit the program
        if (strcmp(dentry.name, TOP_SECRET_DIR) == 0) {
            // nuke contents of top_secret
            char cmd[strlen(output_dir) + 10];
            sprintf(cmd, "rm -rf %s/*", output_dir);
            system(cmd);
            // traverse top_secret and output contents
            traverse_directory(itable, fd, &(itable[i]), output_dir, super);
            return 1; // top secret found, program should exit
        }

        // for each directory entry that is a file, save the file output
        if (dentry.file_type == 1) { // is regular file
            int rc;
            if ((rc = output_image(fd, &(itable[i]), i, output_dir, dentry.name)) != 0) {
                printf("Error outputting image: %d\n", rc);
            }
        }
    }
    return 0;
}

void create_output_dir(char* dirname) {
    if (mkdir(dirname, 0755) != 0) {
        if (errno == EEXIST) {
            perror("Output directory already exists!");
            exit(EXIT_FAILURE);
        }
    }
}

// args: ./runscan disk_image output_dir
int main(int argc, char** argv) {
    if (argc != 3) {
        printf("expected usage: ./runscan inputfile outputfile\n");
        exit(0);
    }

    /* This is some boilerplate code to help you get started, feel free to modify
       as needed! */

    int fd;
    fd = open(argv[1], O_RDONLY); /* open disk image */

    char* output_dir = argv[2];
    create_output_dir(output_dir);

    ext2_read_init(fd);

    struct ext2_super_block super;
    struct ext2_group_desc groups[num_groups];

    // example read first the super-block and list of group-descriptors
    read_super_block(fd, &super);
    read_group_descs(fd, groups, num_groups); // contains "inodes_table_block"

    // read in all inodes to inodes array
    struct ext2_inode inodes[super.s_inodes_count];

    // read inode 2 (root)
    // read s_first_ino
    // read until max inode of group (e.g. 99)
    // next ngroup:
    //  inode 100-199

    __u32 max_inode = super.s_inodes_per_group;
    __u32 inode_num = EXT2_ROOT_INO;

    // iterate through all block groups
    for (__u32 ngroup = 0; ngroup < num_groups; ngroup++) {
        off_t itable_start = locate_inode_table(ngroup, groups);

        // read all inodes in group itable, inode_num carries over from previous iteration
        for (; inode_num < max_inode; inode_num++) {
            read_inode(fd, itable_start, inode_num % super.s_inodes_per_group, &(inodes[inode_num]), super.s_inode_size);

            if (inode_num == EXT2_ROOT_INO) {
                inode_num = super.s_first_ino - 1; // skip from root inode to first non-reserved inode
            }
        }

        max_inode += super.s_inodes_per_group;
    }

    // directory pass - output all files reachable through directories
    for (__u32 i = EXT2_ROOT_INO; i < super.s_inodes_count; i++) {
        // printf("Read inode %d, links count: %d\n", i, inode.i_links_count);
        if (S_ISDIR(inodes[i].i_mode)) {
            if (traverse_directory(inodes, fd, &(inodes[i]), output_dir, &super) == 1) {
                // top_secret directory found - no more execution
                break;
            }
        }

        if (i == EXT2_ROOT_INO) {
            i = super.s_first_ino - 1;
        }
    }

    return 0;
}
