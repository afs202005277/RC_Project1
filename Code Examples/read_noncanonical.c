// Read from serial port in non-canonical mode
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

// Baudrate settings are defined in <asm/termbits.h>, which is
// included by <termios.h>
#define BAUDRATE B38400
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1

#define BUF_SIZE 256

#define FLAG 0x7E
#define COMMAND_SENDER 0x03
#define COMMAND_RECEIVER 0x01
#define SET 0x03
#define UA 0x07

#define SIZE_COMMAND_WEBS 5
enum State
{
    START,
    FLAG_RCV,
    A_RCV,
    C_RCV,
    BCC_OK,
    STOP
};

enum State state = START;

void readPackage(int fd, unsigned char* buffer){
    unsigned char byte = 0;
    do
    {
        switch (state)
        {
        case START:
            if (byte == FLAG)
            {
                state = FLAG_RCV;
            }
            break;
        case FLAG_RCV:
            if (byte == COMMAND_SENDER)
            {
                state = A_RCV;
            }
            else if (byte != FLAG)
            {
                state = START;
            }
            break;
        case A_RCV:
            if (byte == SET)
            {
                state = C_RCV;
            }
            else if (byte == FLAG)
            {
                state = FLAG_RCV;
            }
            else
            {
                state = START;
            }
            break;
        case C_RCV:
            if (byte == FLAG)
            {
                state = FLAG_RCV;
            }
            else if ((COMMAND_SENDER ^ SET) == byte)
            {
                state = BCC_OK;
            }
            else
            {
                state = START;
            }
            break;
        case BCC_OK:
            if (byte == FLAG)
            {
                state = STOP;
            }
            else
            {
                state = START;
            }
            break;
        default:
            break;
        }

    } while (state != STOP);
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

    // Open serial port device for reading and writing and not as controlling tty
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
    newtio.c_cc[VTIME] = 0; // Inter-character timer unused
    newtio.c_cc[VMIN] = 1;  // Blocking read until 5 chars received

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

    // Loop for input
    unsigned char buf[BUF_SIZE] = {0};

    readPackage(fd, buf);

    if (state == STOP){
        unsigned char uaReceive[] = {FLAG, COMMAND_SENDER, UA, COMMAND_SENDER ^ UA, FLAG};
        write(fd, uaReceive, SIZE_COMMAND_WEBS);
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
