// Write to serial port in non-canonical mode
//
// Modified by: Eduardo Nuno Almeida [enalmeida@fe.up.pt]

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>

// Baudrate settings are defined in <asm/termbits.h>, which is
// included by <termios.h>
#define BAUDRATE B38400
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1

#define BUF_SIZE 256

volatile int STOP = FALSE;

#define FLAG 0x7E
#define COMMAND_SENDER 0x03
#define COMMAND_RECEIVER 0x01
#define SET 0x03
#define UA 0x07

#define MAX_REPEAT 3
unsigned char alarmTriggered = FALSE;

void alarmHandler(int num)
{
    alarmTriggered = TRUE;
    printf("ALARM\n");
}

int main(int argc, char *argv[])
{
    unsigned char max_repeat = MAX_REPEAT;
    // Program usage: Uses either COM1 or COM2
    const char *serialPortName = argv[1];

    if (argc < 2)
    {
        printf("Incorrect program usage\n"
               "Usage: %s <SerialPort>\n"
               "Example: %s /dev/ttyS1\n",
               argv[0],
               argv[0]);
        exit(1);
    }

    // Open serial port device for reading and writing, and not as controlling tty
    // because we don't want to get killed if linenoise sends CTRL-C.
    int fd = open(serialPortName, O_RDWR | O_NOCTTY);

    if (fd < 0)
    {
        perror(serialPortName);
        exit(-1);
    }

    struct termios oldtio;
    struct termios newtio;

    // Save current port settings
    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        exit(-1);
    }

    // Clear struct for new port settings
    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    // Set input mode (non-canonical, no echo,...)
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 30; // Inter-character timer unused
    newtio.c_cc[VMIN] = 0;  // Blocking read until 5 chars received

    // VTIME e VMIN should be changed in order to protect with a
    // timeout the reception of the following character(s)

    // Now clean the line and activate the settings for the port
    // tcflush() discards data written to the object referred to
    // by fd but not transmitted, or data received but not read,
    // depending on the value of queue_selector:
    //   TCIFLUSH - flushes data received but not read.
    tcflush(fd, TCIOFLUSH);

    // Set new port settings
    if (tcsetattr(fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    printf("New termios structure set\n");

    // Create string to send
    unsigned char buf[BUF_SIZE] = {0};

    // gets(buf);

    unsigned int length = strlen(buf);

    // In non-canonical mode, '\n' does not end the writing.
    // Test this condition by placing a '\n' in the middle of the buffer.
    // The whole buffer must be sent even with the '\n'.
    // buf[5] = '\n';

    (void)signal(SIGALRM, alarmHandler);

    unsigned char setUp[] = {FLAG, COMMAND_SENDER, SET, COMMAND_SENDER ^ SET, FLAG};
    unsigned char uaReceive[] = {FLAG, COMMAND_SENDER, UA, COMMAND_SENDER ^ UA, FLAG};
    write(fd, setUp, 5);
    while (max_repeat > 0)
    {
        int bytes = 0;
        if (alarmTriggered == TRUE)
            bytes = write(fd, setUp, 5);
        printf("%d bytes written\n", 5);

        alarm(3);

        int bytesRead = read(fd, buf, BUF_SIZE);
        max_repeat--;
        // printf("%s\n", buf);
        if (bytesRead > 0)
        {
            unsigned char equal = TRUE;
            for (int i = 0; i < 5; i++)
            {
                if (buf[i] != uaReceive[i])
                {
                    equal = FALSE;
                    break;
                }
            }
            if (equal == TRUE)
            {
                printf("Equal\n");
                alarm(0);
                break;
            }
            // printf("Received UA but it was wrong.\n");
            printf("Received: ");
            for (int i = 0; i < 5; i++)
            {
                printf("%x", buf[i]);
            }
            printf("\nExpected: ");
            for (int i = 0; i < 5; i++)
            {
                printf("%x", uaReceive[i]);
            }
            printf("\n");
        }
    }
    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    close(fd);

    return 0;
}
