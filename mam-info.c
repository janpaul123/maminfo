#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <scsi/sg_lib.h>
#include <scsi/sg_io_linux.h>

#define READ_ATT_REPLY_LEN 512
#define READ_ATT_CMD_LEN 16
#define EBUFF_SZ 256

#define MAM_TYPE_BINARY          0x00
#define MAM_TYPE_ASCII           0x01

// https://www.ibm.com/support/pages/system/files/support/ssg/ssgdocs.nsf/0/4d430d4b4e1f09b18525787300607b1d/$FILE/LTO%20SCSI%20Reference%20(EXTERNAL%20-%2020171024).pdf
#define MAM_ATT_MANUFACTURER     0x0400
#define MAM_ATT_SERIAL           0x0401
#define MAM_ATT_MANUFACTURE_DATE 0x0406
#define MAM_ATT_LAST_WRITTEN     0x0804
#define MAM_ATT_BARCODE          0x0806
#define MAM_ATT_IDENTIFIER       0x0008
#define MAM_ATT_LOAD_COUNT       0x0003
#define MAM_ATT_INIT_COUNT       0x0007
#define MAM_ATT_TOTAL_MB_WRITTEN 0x0220
#define MAM_ATT_TOTAL_MB_READ    0x0221
#define MAM_ATT_LAST_MB_WRITTEN  0x0222
#define MAM_ATT_LAST_MB_READ     0x0223
#define MAM_ATT_MAXIMUM_CAPACITY 0x0001
#define MAM_ATT_DENSITY_CODE     0x0405


struct globalArgs_t {
  const char* device_name;
} globalArgs;

//---------------Usage--------------
static void usage()
{
    fprintf(stderr, 
          "LTO Medium Access Memory tool\n"
          "Usage: \n"
          "lto-cm -f device [-c] [-v]\n"
          " where:\n"
          "    -f device        is a sg device                        \n"
          "    -h/?             display usage\n"
         );
}

int att_read(int fd, void* data, int command, int len, int data_type){
    int ok;
    unsigned char rAttCmdBlk[READ_ATT_CMD_LEN] = {0x8C, 0x00, 0, 0, 0, 0, 0, 0, 0x04, 0x00, 0, 0, 159,0, 0, 0};
    unsigned char inBuff[READ_ATT_REPLY_LEN];
    unsigned char sense_buffer[32];
    sg_io_hdr_t io_hdr;

    rAttCmdBlk[8]  = 0xff & (command >> 8);
    rAttCmdBlk[9]  = 0xff & command;
    rAttCmdBlk[12] = 0xff & len;

    memset(&io_hdr, 0, sizeof(sg_io_hdr_t));
    io_hdr.interface_id = 'S';
    io_hdr.cmd_len = sizeof(rAttCmdBlk);
    io_hdr.mx_sb_len = sizeof(sense_buffer);
    io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
    io_hdr.dxfer_len = READ_ATT_REPLY_LEN;
    io_hdr.dxferp = inBuff;
    io_hdr.cmdp = rAttCmdBlk;
    io_hdr.sbp = sense_buffer;
    io_hdr.timeout = 20000;     

    if (ioctl(fd, SG_IO, &io_hdr) < 0) {
        perror("SG_READ_ATT_0803: Inquiry SG_IO ioctl error");
        close(fd);
        return -1;
    }

    ok = 0;
    switch (sg_err_category3(&io_hdr)) {
    case SG_LIB_CAT_CLEAN:
        ok = 1;
        break;
    case SG_LIB_CAT_RECOVERED:
        printf("Recovered error on SG_READ_ATT, continuing\n");
        ok = 1;
        break;
    default: /* won't bother decoding other categories */
        printf("ERROR : Problem reading attribute %04x\n", command);
        sg_chk_n_print3("SG_READ_ATT command error", &io_hdr, 1);
        return -1;
    }

    if (ok) { /* output result if it is available */

        printf("SG_READ_ATT command=%0x duration=%u millisecs, resid=%d, msg_status=%d\n",
                command, io_hdr.duration, io_hdr.resid, (int)io_hdr.msg_status);

        int i;
        printf("Raw value for attribute %04x: ", command);
        for(i=0;i<len;i++) {
          printf("%02x ", inBuff[9+i]);
        }
        printf("\n");

        if(data_type == MAM_TYPE_BINARY) {
           int i;
           uint64_t out = 0;
           for(i=0;i<len;i++) {
             out *= 256;
             out += inBuff[9+i];
           }
           memcpy( data, (void*)&out,len );
        }

        if(data_type == MAM_TYPE_ASCII) {
                memcpy(data, &inBuff[9],len );
                ((char*)data)[len] = 0;
        }
   }

return 0;
}


//----------------------------- MAIN FUNCTION---------------------------------
int main(int argc, char * argv[])
{
    int sg_fd;
    int k,i,l;
    char * file_name = 0;
    char ebuff[EBUFF_SZ];
    char messageout[160] ;
    int c=0;

    globalArgs.device_name=NULL;

    while (1) {
        c = getopt(argc, argv, "f:h?vc");

        if (c == -1)
            break;

        switch (c) {
        case 'f':
             if ((globalArgs.device_name=(char*)optarg)==NULL) {
                fprintf(stderr, "ERROR : Specify a device\n");
                usage();
                return SG_LIB_SYNTAX_ERROR;
            }
            break;
        case 'h':
        case '?':
            usage();
            return 0;
        default:
            fprintf(stderr, "ERROR : Unrecognised option code 0x%x ??\n", c);
            usage();
            return SG_LIB_SYNTAX_ERROR;
        }
        
    }

    if (argc == 1) {
        usage();
        return 1;
    }   

    if (optind < argc) {
        for (; optind < argc; ++optind)
            fprintf(stderr, "ERROR : Unexpected extra argument: %s\n",
                    argv[optind]);
        usage();
        return SG_LIB_SYNTAX_ERROR;
    }

    if(!(globalArgs.device_name)){
            usage();
            return SG_LIB_SYNTAX_ERROR;
    }

    if ((sg_fd = open(globalArgs.device_name, O_RDWR)) < 0) {
        snprintf(ebuff, EBUFF_SZ,
                 "ERROR : opening file: %s", file_name);
        perror(ebuff);
        return -1;
    }
    /* Just to be safe, check we have a new sg device by trying an ioctl */
    if ((ioctl(sg_fd, SG_GET_VERSION_NUM, &k) < 0) || (k < 30000)) {
        printf("ERROR :  %s doesn't seem to be an new sg device\n",
               globalArgs.device_name);
        close(sg_fd);
        return -1;
    }

    char manufacturer[9];
    if(att_read(sg_fd, manufacturer, MAM_ATT_MANUFACTURER, 8, MAM_TYPE_ASCII) < 0) {
            printf("ERROR : Read manufacturer failed\n");
    }
    printf("Manufacturer: %.8s\n", manufacturer);

    char serial[33];
    if(att_read(sg_fd, serial, MAM_ATT_SERIAL, 32, MAM_TYPE_ASCII) < 0) {
            printf("ERROR : Read serial failed\n");
    }
    printf("Serial: %.32s\n", serial);

    char manufacture_date[9];
    if(att_read(sg_fd, manufacture_date, MAM_ATT_MANUFACTURE_DATE, 8, MAM_TYPE_ASCII) < 0) {
            printf("ERROR : Read manufacture_date failed\n");
    }
    printf("Manuf. Date:  %.8s\n", manufacture_date);

/*
    char last_written[13];
    if(att_read(sg_fd, last_written, MAM_ATT_LAST_WRITTEN, 12, MAM_TYPE_ASCII) < 0) {
            printf("ERROR : Read last_written failed\n");
            last_written[0] = '\0';
    }
    printf("Last written: %.12s", last_written);
*/
    char barcode[33];
    if(att_read(sg_fd, barcode, MAM_ATT_BARCODE, 12, MAM_TYPE_ASCII) < 0) {
            printf("ERROR : Read barcode failed\n");
            barcode[0] = '\0';
    }
    printf("Barcode: %.12s\n", barcode);

/*
    char identifier[33];
    if(att_read(sg_fd, identifier, MAM_ATT_IDENTIFIER, 32, MAM_TYPE_ASCII) < 0) {
            printf("ERROR : Read identifier failed\n");
            identifier[0] = '\0';
    }
    printf("Identifier: %.32s\n", identifier);
*/

/*
    uint64_t load_count;
    if(att_read(sg_fd, &load_count, MAM_ATT_LOAD_COUNT, 8, MAM_TYPE_BINARY) < 0) {
            printf("ERROR : Read failed\n");
    }
*/
    uint16_t init_count;
    if(att_read(sg_fd, &init_count, MAM_ATT_INIT_COUNT, 2, MAM_TYPE_BINARY) < 0) {
            printf("ERROR : Read init_count failed\n");
    }
    printf("Init count: %d\n", init_count);

    uint64_t total_mb_written;
    if(att_read(sg_fd, &total_mb_written, MAM_ATT_TOTAL_MB_WRITTEN, 8, MAM_TYPE_BINARY) < 0) {
            printf("ERROR : Read total_mb_written failed\n");
    }
    printf("Total MB written: %ld\n", total_mb_written);

    uint64_t total_mb_read;
    if(att_read(sg_fd, &total_mb_read, MAM_ATT_TOTAL_MB_READ, 8, MAM_TYPE_BINARY) < 0) {
            printf("ERROR : Read total_mb_read failed\n");
    }
    printf("Total MB read: %ld\n", total_mb_read);

    uint64_t last_mb_written;
    if(att_read(sg_fd, &last_mb_written, MAM_ATT_LAST_MB_WRITTEN, 8, MAM_TYPE_BINARY) < 0) {
            printf("ERROR : Read last_mb_written failed\n");
    }
    printf("Last MB written: %ld\n", last_mb_written);

    uint64_t last_mb_read;
    if(att_read(sg_fd, &last_mb_read, MAM_ATT_LAST_MB_READ, 8, MAM_TYPE_BINARY) < 0) {
            printf("ERROR : Read last_mb_read failed\n");
    }
    printf("Last MB read: %ld\n", last_mb_read);

    /*
    uint64_t maximum_capcity;
    if(att_read(sg_fd, &maximum_capcity, MAM_ATT_MAXIMUM_CAPACITY, 8, MAM_TYPE_BINARY) < 0) {
            printf("ERROR : Read maximum_capcity failed\n");
    }
    printf("Maximum capacity in MB: %ld\n", maximum_capcity);
    */

    // ULTRIUM 3: 44h
    // ULTRIUM 4: 46h
    // ULTRIUM 5: 58h
    // ULTRIUM 6: 5Ah
    // ULTRIUM 7: 5Ch
    // ULTRIUM M8: 5Dh
    // ULTRIUM 8: 5Eh
    char density_code;
    if(att_read(sg_fd, &density_code, MAM_ATT_DENSITY_CODE, 1, MAM_TYPE_BINARY) < 0) {
            printf("ERROR : Read density_code failed\n");
    }
    printf("Density code: %02X\n", density_code);

    close(sg_fd);

    return 0;
}
