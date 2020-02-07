#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/i2c-dev.h>
#include <linux/un.h>
#include <syslog.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <i2c/smbus.h>
#include <gpiod.h>
#include <unistd.h>

// ES100 API register addresses
#define ES100_CONTROL0_REG       0x00
#define ES100_CONTROL1_REG       0x01
#define ES100_IRQ_STATUS_REG     0x02
#define ES100_STATUS0_REG        0x03
#define ES100_YEAR_REG           0x04
#define ES100_MONTH_REG          0x05
#define ES100_DAY_REG            0x06
#define ES100_HOUR_REG           0x07
#define ES100_MINUTE_REG         0x08
#define ES100_SECOND_REG         0x09
#define ES100_NEXT_DST_MONTH_REG 0x0A
#define ES100_NEXT_DST_DAY_REG   0x0B
#define ES100_NEXT_DST_HOUR_REG  0x0C
#define ES100_DEVICE_ID_REG      0x0D
#define ES100_RESERVED_1_REG     0x0E
#define ES100_RESERVED_2_REG     0x0F

#define ES100_REG_LENGTH         16

#define MY_NAME "es100"

#ifdef __x86_64__
#define I2C_BUS "/dev/i2c-0"
#define GPIO_CHIP "INT33FC:02"
#define GPIO_IRQ 0
#define GPIO_ENABLE 1
#endif

#ifdef __arm__
#define I2C_BUS "/dev/i2c-1"
#define GPIO_CHIP "pinctrl-bcm2835"
#define GPIO_IRQ 4
#define GPIO_ENABLE 17
#endif


#define SOCK_MAGIC 0x534f434b

struct sock_sample
  {
    struct timeval tv;
    double offset;
    int pulse;
    int leap;
    int _pad;
    int magic;
  };


struct shmTime {
        int    mode; /* 0 - if valid is set:
                      *       use values,
                      *       clear valid
                      * 1 - if valid is set:
                      *       if count before and after read of data is equal:
                      *         use values
                      *       clear valid
                      */
        volatile int    count;
        time_t          clockTimeStampSec;
        int             clockTimeStampUSec;
        time_t          receiveTimeStampSec;
        int             receiveTimeStampUSec;
        int             leap;
        int             precision;
        int             nsamples;
        volatile int    valid;
        unsigned        clockTimeStampNSec;     /* Unsigned ns timestamps */
        unsigned        receiveTimeStampNSec;   /* Unsigned ns timestamps */
        int             dummy[8];
};


static void mc_shm_write( struct shmTime *mc_shmTime_ptr, struct tm *mc_tm_es100_ptr, struct gpiod_line_event *mc_gpiod_event_ptr, int leap_notify)
  {
    mc_shmTime_ptr->valid = 0;
    mc_shmTime_ptr->mode = 0;
    mc_shmTime_ptr->count = 0;
    mc_shmTime_ptr->leap = leap_notify;
    mc_shmTime_ptr->precision = 0;
    mc_shmTime_ptr->clockTimeStampSec = mktime(mc_tm_es100_ptr);
    mc_shmTime_ptr->clockTimeStampUSec = 0;
    mc_shmTime_ptr->clockTimeStampNSec = 0;
    mc_shmTime_ptr->receiveTimeStampSec = mc_gpiod_event_ptr->ts.tv_sec;
    mc_shmTime_ptr->receiveTimeStampUSec = mc_gpiod_event_ptr->ts.tv_nsec/1000;
    mc_shmTime_ptr->receiveTimeStampNSec = mc_gpiod_event_ptr->ts.tv_nsec;
    mc_shmTime_ptr->valid = 1;
  }


static void mc_sock_write( int mc_sockfd, const struct sockaddr_un *dest_addr, socklen_t addrlen, struct tm *mc_tm_es100_ptr, struct gpiod_line_event *mc_gpiod_event_ptr, int leap_notify)
  {
    struct sock_sample mc_sample;

    mc_sample.pulse = 0;
    mc_sample.leap = leap_notify;
    mc_sample.magic = SOCK_MAGIC;
    mc_sample._pad = 0;
    mc_sample.tv.tv_sec = mc_gpiod_event_ptr->ts.tv_sec;
    mc_sample.tv.tv_usec = mc_gpiod_event_ptr->ts.tv_nsec/1000;
    mc_sample.offset = mktime(mc_tm_es100_ptr) - mc_gpiod_event_ptr->ts.tv_sec;
    mc_sample.offset -= mc_gpiod_event_ptr->ts.tv_nsec/1e9;

    if( sendto( mc_sockfd, &mc_sample, sizeof(mc_sample), 0,  (struct sockaddr *)dest_addr, addrlen) == -1)
      {
        syslog( LOG_INFO, "Failed socket sendto()");
        exit(EXIT_FAILURE);
      }
  }


static void daemonize()
  {
    pid_t pid;
    int i;

    /* Fork off the parent process */
    if( (pid = fork()) < 0) exit(EXIT_FAILURE);

    /* Success: Let the parent terminate */
    if (pid > 0) exit(EXIT_SUCCESS);

    /* On success: The child process becomes session leader */
    if (setsid() < 0) exit(EXIT_FAILURE);

    // Catch, ignore and handle signals
    // todo: Implement a working signal handler */
    // signal(SIGCHLD, SIG_IGN);
    // signal(SIGHUP, SIG_IGN);

    /* Fork off for the second time*/
    if( (pid = fork()) < 0) exit(EXIT_FAILURE);

    /* Success: Let the parent terminate */
    if (pid > 0) exit(EXIT_SUCCESS);

    /* Set new file permissions */
    umask(0);

    /* Change the working directory to the root directory */
    /* or another appropriated directory */
    chdir("/");

    /* Close all open file descriptors */
    for (i = sysconf(_SC_OPEN_MAX); i>=0; i--)
      close (i);
  }  // daemonize()


/*
 * Copy src to string dst of size siz.  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz == 0).
 * Returns strlen(src); if retval >= siz, truncation occurred.
 */
size_t strlcpy(char *dst, const char *src, size_t siz)
  {
    char *d = dst;
    const char *s = src;
    size_t n = siz;

    /* Copy as many bytes as will fit */
    if (n != 0 && --n != 0)
      {
        do
          {
            if ((*d++ = *s++) == 0)
              break;
           } while (--n != 0);
      }

    /* Not enough room in dst, add NUL and traverse rest of src */
    if (n == 0)
      {
        if (siz != 0)
          *d = '\0';                /* NUL-terminate dst */
        while (*s++)
          ;
      }

    return(s - src - 1);        /* count does not include NUL */
  }


int main( int argc, char** argv)
  {
    int i2c_device_file, i, opt, mc_shmid, mc_sockfd, mc_tracking=0, shm_unit;
    int shm_enable=0, sock_enable=0;
    __s32 character;
    __u8 mc_es100_regs[ES100_REG_LENGTH];
    char mc_log_output[60];
    char *mc_sock_path;

    struct gpiod_chip *mc_gpio_chip;
    struct gpiod_line *mc_gpio_enable, *mc_gpio_irq;
    struct gpiod_line_event mc_gpiod_event;

    const struct timespec timeout = {180,0};
    const struct timespec enable_delay = {0,150*1e6};  // 150 milliseconds
    struct tm mc_tm_es100;

    struct shmTime *mc_shmTime;
    struct sockaddr_un mc_sockaddr;

    // Parse command line
    while( (opt = getopt(argc, argv, "c:s:")) != -1)
      {
        switch(opt)
          {
            case 'c':
              sock_enable=1;
              mc_sock_path = optarg;
              break;
            case 's':
              shm_unit = atoi(optarg);
              if( (shm_unit > 207) | (shm_unit < 0))
                {
                  fprintf( stderr, "shm_unit out of range\n");
                  exit(EXIT_FAILURE);
                }
              shm_enable = 1;
              break;
            default: /* '?' */
              fprintf( stderr, "Usage: %s [-c chrony socket|-s shm_unit]\n", argv[0]);
              exit(EXIT_FAILURE);
           }
       }

    // Daemonize
    daemonize();

    // syslog
    openlog( MY_NAME, LOG_PID, LOG_USER);
    syslog( LOG_INFO, "es100 started.");

    // mktime uses local time
    setenv("TZ", "UTC", 1);

    // set up shared memory
    if( shm_enable)
      {
        mc_shmid = shmget (0x4e545030+shm_unit, sizeof (struct shmTime), IPC_CREAT|(shm_unit<2?0600:0666));
        if (mc_shmid == -1) exit(EXIT_FAILURE);
        mc_shmTime = (struct shmTime *)shmat( mc_shmid, 0, 0);
        if (mc_shmTime == (void *)-1) exit(EXIT_FAILURE);
        mc_shmTime->valid = 0;
        syslog( LOG_INFO, "Set up shared memory unit %d", shm_unit);
      }
    else
      syslog( LOG_INFO, "Not using shared memory");

    // set up chrony socket
    if (sock_enable)
      {
        mc_sockfd = socket( AF_UNIX, SOCK_DGRAM, 0);
        if (mc_sockfd < 0)
          {
            syslog( LOG_INFO, "Create socket failed.");
            exit(EXIT_FAILURE);
          }

        memset( &mc_sockaddr, 0, sizeof(struct sockaddr_un));
        mc_sockaddr.sun_family = AF_UNIX;
        (void)strlcpy( mc_sockaddr.sun_path, mc_sock_path, sizeof(mc_sockaddr.sun_path));

        syslog( LOG_INFO, "Set up chrony socket %s", mc_sock_path);
      }
    else
      syslog( LOG_INFO, "Not using chrony socket");

    // Set up GPIO
    if( (mc_gpio_chip = gpiod_chip_open_lookup( GPIO_CHIP)) == NULL) exit(EXIT_FAILURE);
    if( (mc_gpio_irq = gpiod_chip_get_line( mc_gpio_chip, GPIO_IRQ)) == NULL) exit(EXIT_FAILURE);
    if( (mc_gpio_enable = gpiod_chip_get_line( mc_gpio_chip, GPIO_ENABLE)) == NULL) exit(EXIT_FAILURE);
    gpiod_line_request_falling_edge_events( mc_gpio_irq, MY_NAME );
    gpiod_line_request_output( mc_gpio_enable, MY_NAME, 0);

    gpiod_line_set_value( mc_gpio_enable, 0);  // turn off the es100 on startup
    nanosleep( &enable_delay, NULL);  // Wait a little bit for the es100 to turn itself off.
    gpiod_line_set_value( mc_gpio_enable, 1);  // gpio enable es100
    nanosleep( &enable_delay, NULL);  // es100 enable delay

    syslog( LOG_INFO, "Set up GPIO " GPIO_CHIP);

    if ((i2c_device_file = open( I2C_BUS, O_RDWR )) < 0) exit(EXIT_FAILURE);
    syslog( LOG_INFO, "Opened i2c bus " I2C_BUS);

    if ( ioctl( i2c_device_file, I2C_SLAVE, 0x32) < 0) exit(EXIT_FAILURE);
    syslog( LOG_INFO, "Opened i2c device 0x32");


    while(1)
      {
        // If tracking, make sure we line up with the 55-second boundry
        if(mc_tracking)
          {
            if( mc_tm_es100.tm_sec > 54)  // Missed it. Wrap over to the next minute.
              mc_tm_es100.tm_min++;
            sleep(( 60+54-mc_tm_es100.tm_sec) % 60);

            if( (mc_tm_es100.tm_min%30) == 9)  // Skip the 6-minute frame
              {
                sleep( 6*60 );
                mc_tm_es100.tm_min += 6;
              }
          }

        // If not tracking, start receiving on both antennas
        // If tracking, pick the antenna that had a succesful reception
        if (i2c_smbus_write_byte_data( i2c_device_file, ES100_CONTROL0_REG, mc_tracking ? ((mc_es100_regs[ES100_STATUS0_REG]&0b00000010) ? 0x13 : 0x15) : 0x01) < 0)
          {
            syslog( LOG_INFO, "Failed i2c_smbus_write_byte_data() to start reception");
            exit(EXIT_FAILURE);
          }
        do
          {
            if ( gpiod_line_event_wait( mc_gpio_irq, &timeout) != 1)
              {
                syslog( LOG_INFO, "Failed gpiod_line_event_wait()");
                exit(EXIT_FAILURE);
              }
            gpiod_line_event_read( mc_gpio_irq, &mc_gpiod_event);

            if (i2c_smbus_write_byte( i2c_device_file, ES100_CONTROL0_REG) < 0)
              {
                syslog( LOG_INFO, "Failed i2c_smbus_write_byte()");
                exit(EXIT_FAILURE);
              }

            for ( i = 0 ; i < ES100_REG_LENGTH; i++)
              {
                if ((character = i2c_smbus_read_byte( i2c_device_file)) < 0)
                  {
                    syslog( LOG_INFO, "Failed i2c_smbus_read_byte()");
                    exit(EXIT_FAILURE);
                  }
                mc_es100_regs[i] = (__u8) character;
                sprintf( mc_log_output+i*3, "%02hhx ", (unsigned char) mc_es100_regs[i]);
              }

            if (mc_tracking)
              {
                if( (mc_es100_regs[ES100_STATUS0_REG] & 0b10000001) == 0b10000001)  // successful tracking operation
                  {
                    mc_tm_es100.tm_sec  = mc_es100_regs[ES100_SECOND_REG] - 6 * (mc_es100_regs[ES100_SECOND_REG] >> 4);
                    mc_tm_es100.tm_min++;  // don't worry if this goes over 59.  mktime() will normalize it.

                    if (shm_enable)
                      mc_shm_write( mc_shmTime, &mc_tm_es100, &mc_gpiod_event, 0);
                    if (sock_enable)
                      mc_sock_write( mc_sockfd, &mc_sockaddr, sizeof(struct sockaddr_un),  &mc_tm_es100, &mc_gpiod_event, 0);

                    syslog( LOG_INFO, "%s Tracking.  IRQ time: %lld.%.9ld  es100 time: %lld ", mc_log_output, (long long)mc_gpiod_event.ts.tv_sec, mc_gpiod_event.ts.tv_nsec, (long long)mktime(&mc_tm_es100));
                  }
                else
                  {
                    syslog( LOG_INFO, "%s Tracking.  IRQ time: %lld.%.9ld", mc_log_output, (long long)mc_gpiod_event.ts.tv_sec, mc_gpiod_event.ts.tv_nsec);
                    mc_tracking = 0;
                  }
              }
            else // !mc_tracking
              {
                if( mc_es100_regs[ES100_IRQ_STATUS_REG] == 0x01)  // successful one-minute reception
                  {
                    mc_tm_es100.tm_sec  = mc_es100_regs[ES100_SECOND_REG] - 6 * (mc_es100_regs[ES100_SECOND_REG] >> 4);
                    mc_tm_es100.tm_min  = mc_es100_regs[ES100_MINUTE_REG] - 6 * (mc_es100_regs[ES100_MINUTE_REG] >> 4);
                    mc_tm_es100.tm_hour = mc_es100_regs[ES100_HOUR_REG] - 6 * (mc_es100_regs[ES100_HOUR_REG] >> 4);
                    mc_tm_es100.tm_mday = mc_es100_regs[ES100_DAY_REG] - 6 * (mc_es100_regs[ES100_DAY_REG] >> 4);
                    mc_tm_es100.tm_mon  = mc_es100_regs[ES100_MONTH_REG] - 6 * (mc_es100_regs[ES100_MONTH_REG] >> 4) - 1;
                    mc_tm_es100.tm_year = mc_es100_regs[ES100_YEAR_REG] - 6 * (mc_es100_regs[ES100_YEAR_REG] >> 4) + 100;
                    mc_tm_es100.tm_isdst = 0;

                    int leapsecond;
                    switch (mc_es100_regs[ES100_STATUS0_REG] & 0b00011000)
                      {
                        case 0b00000000:
                        case 0b00001000:
                          leapsecond = 0;
                          break;
                        case 0b00010000:
                          leapsecond = 2;
                          break;
                        case 0b00011000:
                          leapsecond = 1;
                          break;
                      }

                    if (shm_enable)
                      mc_shm_write( mc_shmTime, &mc_tm_es100, &mc_gpiod_event, leapsecond );
                    if (sock_enable)
                      mc_sock_write( mc_sockfd, &mc_sockaddr, sizeof(struct sockaddr_un),  &mc_tm_es100, &mc_gpiod_event, 0);

                    syslog( LOG_INFO, "%s 1-minute.  IRQ time: %lld.%.9ld  es100 time: %lld ", mc_log_output, (long long)mc_gpiod_event.ts.tv_sec, mc_gpiod_event.ts.tv_nsec, (long long)mktime(&mc_tm_es100));

                    mc_tracking = 1;
                  }
                else
                  {
                    syslog( LOG_INFO, "%s 1-minute.  IRQ time: %lld.%.9ld", mc_log_output, (long long)mc_gpiod_event.ts.tv_sec, mc_gpiod_event.ts.tv_nsec);
                  }
              }
          } while (mc_es100_regs[ES100_IRQ_STATUS_REG] != 0x01);
      }  // while(1)
  }  // main
