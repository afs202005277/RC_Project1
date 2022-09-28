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

#define SIZE_COMMAND_WEBS 5
#define MAX_REPEAT 3
unsigned char alarmTriggered = FALSE;

void alarmHandler(int num)
{
    alarmTriggered = TRUE;
    printf("ALARM\n");
}

int flag_check(unsigned char *v1, unsigned char *v2, unsigned int numBytes)
{
    unsigned char equal = TRUE;
    for (int i = 0; i < numBytes; i++)
    {
        if (v1[i] != v2[i])
        {
            equal = FALSE;
            break;
        }
    }
    if (equal == FALSE)
    {
        printf("Received UA but it was wrong.\n");
        printf("Received: ");
        for (int i = 0; i < numBytes; i++)
        {
            printf("%x", v1[i]);
        }
        printf("\nExpected: ");
        for (int i = 0; i < numBytes; i++)
        {
            printf("%x", v2[i]);
        }
        printf("\n");
    }
    return equal;
}

int main(int argc, char *argv[])
{
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
    newtio.c_cc[VMIN] = 0;   // Blocking read until 5 chars received

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

    unsigned char buf[BUF_SIZE] = {0};
    (void)signal(SIGALRM, alarmHandler);
    unsigned char setUp[] = {FLAG, COMMAND_SENDER, SET, COMMAND_SENDER ^ SET, FLAG};
    unsigned char uaReceive[] = {FLAG, COMMAND_SENDER, UA, COMMAND_SENDER ^ UA, FLAG};
    unsigned char max_repeat = MAX_REPEAT;
    if (write(fd, setUp, SIZE_COMMAND_WEBS) == -1)
    {
        perror("Couldn't write to the serial port: ");
    }
    while (max_repeat > 0)
    {
        if (alarmTriggered == TRUE)
            if (write(fd, setUp, SIZE_COMMAND_WEBS) == -1)
            {
                perror("Couldn't write to the serial port: ");
            }
        alarm(3);

        int bytesRead = read(fd, buf, BUF_SIZE);
        max_repeat--;
        if (bytesRead > 0 && flag_check(buf, uaReceive, SIZE_COMMAND_WEBS) == TRUE)
        {
            printf("Logical connection established successfully!\n");
            alarm(0);
            break;
        }
    }
    if (max_repeat == 0)
        printf("Timeout too many times! Giving up...\n");
    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    close(fd);

    return 0;
}
