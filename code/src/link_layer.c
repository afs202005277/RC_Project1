// Link layer protocol implementation

/*TO DO:
- adicionar read
- funcao sendFrame e receiveFrame
- Protocolo de ligacao de dados
- fazer disconnect
*/

#include "link_layer.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>

#define ESCAPE 0x7D
#define FLAG 0x7E
#define COMMAND_SENDER 0x03
#define COMMAND_RECEIVER 0x01
#define SET 0x03
#define DISC 0x0B
#define UA 0x07
#define RR_0 0x05
#define RR_1 0x85
#define REJ_0 0x01
#define REJ_1 0x81
#define SD_0 0x00
#define SD_1 0x40

#define MAX_BYTES 2001

#define SIZE_COMMAND_WEBS 5

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source

static int fd = -1;

enum Next_Step
{
    SEND_UA,
    SEND_DATA_0,
    SEND_DATA_1,
    SEND_RR_0,
    SEND_RR_1,
    SEND_DISC
};

enum State
{
    START,
    FLAG_RCV,
    A_RCV,
    C_RCV,
    BCC_OK,
    SUCCESS
};

enum Connection_State
{
    ESTABLISHMENT,
    DATA_TRANSFER,
    TERMINATION
};

static enum State state = START;
static enum Connection_State connection_state = ESTABLISHMENT;
static enum Next_Step next_step;

static unsigned char alarmTriggered = FALSE;
static unsigned char attempts = 0;

static int maxRepeat;
static int timeout;
static LinkLayerRole role;

void alarmHandler(int num)
{
    alarmTriggered = TRUE;
    printf("Attempt #%d failed!\n", attempts);
}

int makeConnection()
{
    unsigned char setUp = {FLAG, COMMAND_SENDER, SET, COMMAND_SENDER ^ SET, FLAG};

    llwrite(setUp, 5);
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
{
    fd = open(connectionParameters.serialPort, O_RDWR | O_NOCTTY);

    if (fd < 0)
    {
        perror(connectionParameters.serialPort);
        exit(-1);
    }
    maxRepeat = connectionParameters.nRetransmissions;
    timeout = connectionParameters.timeout;
    role = connectionParameters.role;
    struct termios oldtio;
    struct termios newtio;

    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        exit(-1);
    }

    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = connectionParameters.baudRate | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;
    newtio.c_lflag = 0;
    if (connectionParameters.role == LlTx)
    {
        newtio.c_cc[VTIME] = 30;
        newtio.c_cc[VMIN] = 0;
    }
    else
    {
        newtio.c_cc[VTIME] = 0;
        newtio.c_cc[VMIN] = 1;
    }

    tcflush(fd, TCIOFLUSH);

    if (tcsetattr(fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    printf("New termios structure set\n");

    if (connectionParameters.role == LlTx)
    {
        (void)signal(SIGALRM, alarmHandler);
        makeConnection();
    }

    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    return 0;
}

void stuffing(const unsigned char *buf, int bufSize, unsigned char *stuffed, int *stuffedSize)
{
    stuffed = malloc(2 * bufSize * sizeof(unsigned char));
    int stuf_idx = 0;
    for (int i = 0; i < bufSize; i++)
    {
        if (buf[i] == FLAG || buf[i] == ESCAPE)
        {
            stuffed[stuf_idx] = ESCAPE;
            stuf_idx++;
        }
        stuffed[stuf_idx] = buf[i];
        stuf_idx++;
    }
    *stuffedSize = stuf_idx;
}

void unstuff(const unsigned char *buf, int bufSize, unsigned char *unstuffed, int *unstuffedSize)
{
    unstuffed = malloc(bufSize * sizeof(unsigned char));
    int unstuf_idx = 0;
    for (int i = 0; i < bufSize - 1; i++)
    {
        if (buf[i] == ESCAPE)
        {
            unstuffed[unstuf_idx] = buf[i + 1];
            i++;
        }
        else
        {
            unstuffed[unstuf_idx] = buf[i];
        }
        unstuf_idx++;
    }
    *unstuffedSize = unstuf_idx;
}

unsigned char bcc2(const unsigned char *buf, int bufSize)
{
    unsigned char result = buf[0];
    for (int i = 1; i < bufSize; i++)
    {
        result ^= buf[i];
    }
    return result;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize)
{
    attempts = 0;
    if (write(fd, buf, bufSize) != bufSize)
    {
        perror("Couldn't write to the serial port: ");
    }
    if (role == LlTx)
    {
        alarm(timeout);
        while (attempts < maxRepeat)
        {
            attempts++;
            if (alarmTriggered == TRUE)
            {
                if (write(fd, buf, bufSize) != bufSize)
                {
                    perror("Couldn't write to the serial port: ");
                }
            }
            llread(buf);
            if (state == SUCCESS)
            {
                if (connection_state == ESTABLISHMENT)
                {
                    connection_state = DATA_TRANSFER;
                    printf("Logical connection established successfully!\n");
                }
                alarm(0);
                return bufSize;
            }
        }
        printf("Giving up...\n");
        return -1;
    }
    return bufSize;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    packet = malloc(MAX_BYTES * sizeof(unsigned char));
    unsigned int idx = 0;
    unsigned char byte = 0;
    do
    {
        if (read(fd, &byte, 1) == 0)
            break;
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
            else if (byte == COMMAND_RECEIVER && role == LlTx && connection_state == TERMINATION)
            {
                state = A_RCV;
                next_step = SEND_UA;
            }
            break;
        case A_RCV:
            if (role == LlTx && byte == UA)
            {
                state = C_RCV;
                next_step = SEND_DATA_0;
            }
            else if (role == LlRx && byte == SET)
            {
                state = C_RCV;
                next_step = SEND_UA;
            }
            else if (byte == FLAG)
            {
                state = FLAG_RCV;
            }
            else if (byte == DISC)
            {
                state = C_RCV;
                if (role == LlRx)
                    next_step = SEND_DISC;
                else
                    next_step = SEND_UA;
            }
            else if (byte == RR_0)
            {
                next_step = SEND_DATA_0;
                state = C_RCV;
            }
            else if (byte == RR_1)
            {
                next_step = SEND_DATA_1;
                state = C_RCV;
            }
            else if (byte == REJ_0)
            {
                next_step = SEND_DATA_0;
                state = C_RCV;
            }
            else if (byte == REJ_1)
            {
                next_step = SEND_DATA_1;
                state = C_RCV;
            }
            else if (byte == SD_0)
            {
                next_step = SEND_RR_1;
                state = C_RCV;
            }
            else if (byte == SD_1)
            {
                next_step = SEND_RR_0;
                state = C_RCV;
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
            else if ((COMMAND_SENDER ^ UA) == byte || (COMMAND_SENDER ^ SET) == byte || (COMMAND_SENDER ^ DISC) == byte || (COMMAND_SENDER ^ RR_0) == byte || (COMMAND_SENDER ^ RR_1) == byte || (COMMAND_SENDER ^ REJ_0) == byte || (COMMAND_SENDER ^ REJ_1) == byte || (COMMAND_SENDER ^ SD_0) == byte || (COMMAND_SENDER ^ SD_1) == byte)
            {
                state = BCC_OK;
            }
            else if (byte == COMMAND_RECEIVER ^ DISC && role == LlTx && connection_state == TERMINATION)
            {
                state = BCC_OK;
                next_step = SEND_UA;
            }
            else
            {
                state = START;
            }
            break;
        case BCC_OK:
            if (byte == FLAG)
            {
                if (idx == 0)
                    state = SUCCESS;
                else if (packet[idx - 1] != ESCAPE)
                {
                    unsigned char *unstuffed_data;
                    int size;
                    unstuff(packet, idx - 1, unstuffed_data, &size);
                    if (idx != 0 && packet[idx - 1] == bcc2(unstuffed_data, size))
                    {
                        idx--;
                        state = SUCCESS;
                    }
                }
                else
                {
                    packet[idx] = byte;
                    idx++;
                }
            }
            else
            {
                packet[idx] = byte;
                idx++;
            }
            break;
        default:
            break;
        }

    } while (state != SUCCESS);
    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    // mandar disc
    close(fd);

    return 1;
}
