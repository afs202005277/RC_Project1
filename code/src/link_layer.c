// Link layer protocol implementation

/*TO DO:
- adicionar read
- funcao sendFrame e receiveFrame
- Protocolo de ligacao de dados
- fazer disconnect
*/

#include "link_layer.h"
#include "tmp.h"

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

static int fd = 0;

enum Possible_Step
{
    SEND_UA,
    SEND_DATA_0,
    SEND_DATA_1,
    SEND_RR_0,
    SEND_RR_1,
    SEND_DISC,
    DO_NOTHING
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
static enum Possible_Step prev_step;
static enum Possible_Step next_step;

unsigned char *unstuffed_buffer;

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
    unsigned char setUp[] = {FLAG, COMMAND_SENDER, SET, COMMAND_SENDER ^ SET, FLAG};

    return llwrite(setUp, 5);
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
        newtio.c_cc[VTIME] = 10;
        printf("vtime: %d\n", newtio.c_cc[VTIME]);
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
        if (makeConnection() == -1)
            return -1;
        else
        {
            connection_state = DATA_TRANSFER;
            printf("Logical connection established successfully!\n");
        }
    }
    else
    {
        unsigned char *packet = NULL;
        llread(packet);
        if (state == SUCCESS)
        {
            if (handleNextStep(NULL, 0) == 0)
            {
                connection_state = DATA_TRANSFER;
                printf("Logical connection established successfully!\n");
            }
        }
    }

    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }
    return 0;
}

unsigned char *stuffing(const unsigned char *buf, int bufSize, int *stuffedSize)
{
    unsigned char *stuffed = malloc(2 * bufSize * sizeof(unsigned char));
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
    return stuffed;
}

unsigned char *unstuff(const unsigned char *buf, int bufSize, int *unstuffedSize)
{
    unsigned char *unstuffed = malloc(bufSize * sizeof(unsigned char));
    int unstuf_idx = 0;
    if (bufSize == 1)
    {
        unstuffed[0] = buf[0];
        *unstuffedSize = 1;
        return unstuffed;
    }
    for (int i = 0; i < bufSize; i++)
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
    return unstuffed;
}

unsigned char bcc2(const unsigned char *buf, int bufSize)
{
    unsigned char result = 0;
    for (int i = 0; i < bufSize; i++)
    {
        result ^= buf[i];
    }
    return result;
}

// falta completar o disc (são precisas 3 mensagens)
int handleNextStep(const unsigned char *buf, int bufSize)
{
    int res = 0;
    switch (next_step)
    {
    case SEND_UA:
        unsigned char uaReceive[] = {FLAG, COMMAND_SENDER, UA, COMMAND_SENDER ^ UA, FLAG};
        res = llwrite(uaReceive, 5);
        break;
    case SEND_DATA_0:
        if (prev_step == SEND_DATA_0)
        {
            res = llwrite(buf, bufSize);
        }
        else
        {
            prev_step = SEND_DATA_0;
            next_step = DO_NOTHING;
        }
        break;
    case SEND_DATA_1:
        if (prev_step == SEND_DATA_1)
        {
            res = llwrite(buf, bufSize);
        }
        else
        {
            prev_step = SEND_DATA_1;
            next_step = DO_NOTHING;
        }
        break;
    case SEND_RR_0:
        unsigned char rr0[] = {FLAG, COMMAND_SENDER, RR_0, COMMAND_SENDER ^ RR_0, FLAG};
        res = llwrite(rr0, 5);
        break;
    case SEND_RR_1:
        unsigned char rr1[] = {FLAG, COMMAND_SENDER, RR_1, COMMAND_SENDER ^ RR_1, FLAG};
        res = llwrite(rr1, 5);
        break;
    case SEND_DISC:
        unsigned char disc[] = {FLAG, COMMAND_SENDER, DISC, COMMAND_SENDER ^ DISC, FLAG};
        res = llwrite(disc, 5);
        break;
    default:
        res = 0;
    }
    if (res >= 0)
        return 0;
    else
        return -1;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////

int sendInformationFrame(const unsigned char *buf, int bufsize)
{
    int stuffedSize = 0;
    unsigned char *stuffed = stuffing(buf, bufsize, &stuffedSize);
    unsigned char *frame = malloc(stuffedSize + 6);
    frame[0] = FLAG;
    frame[1] = COMMAND_SENDER;
    if (next_step == SEND_DATA_0)
    {
        frame[2] = 0x00;
    }
    else if (next_step == SEND_DATA_1)
    {
        frame[2] = 0x40;
    }
    frame[3] = COMMAND_SENDER ^ frame[2];
    for (int i = 0; i < stuffedSize; i++)
    {
        frame[4 + i] = stuffed[i];
    }
    frame[4 + stuffedSize] = bcc2(buf, bufsize);
    frame[4 + stuffedSize + 1] = FLAG;
    next_step = (prev_step == SEND_DATA_0 ? SEND_DATA_1 : SEND_DATA_0);
    return handleNextStep(frame, stuffedSize + 6);
}

void printBuffer(const unsigned char *buf, int bufsize)
{
    for (int i = 0; i < bufsize; i++)
    {
        printf("%x ", buf[i]);
    }
    printf("\n");
}

int llwrite(const unsigned char *buf, int bufSize)
{
    attempts = 0;
    if (role == LlTx)
    {
        do
        {
            alarm(timeout);
            if (write(fd, buf, bufSize) != bufSize)
            {
                printf("fd: %d\n", fd);
                printf("bufSize: %d\nbuf: ", bufSize);
                printBuffer(buf, bufSize);
                perror("Couldn't write to the serial port: ");
                continue;
            }

            if (llread(NULL) != -1)
            {
                alarm(0);
                handleNextStep(buf, bufSize);
                return bufSize;
            }
            attempts++;
        } while (attempts < maxRepeat);
    }
    else if (role == LlRx)
    {
        if (write(fd, buf, bufSize) < 0)
        {
            printf("fd: %d\n", fd);
            printf("bufSize: %d\nbuf: ", bufSize);
            printBuffer(buf, bufSize);
            perror("Couldn't write to the serial port: ");
            return -1;
        }
        return bufSize;
    }
    return -1;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int getInformationFrame(unsigned char *buf)
{
    unsigned char *tmp = malloc(MAX_BYTES);
    unstuffed_buffer = malloc(MAX_BYTES);
    int num_bytes = llread(tmp);
    memcpy(buf, unstuffed_buffer, num_bytes);
    return num_bytes;
}

int trans_read()
{
    state = START;
    unsigned char byte = 0;
    unsigned char gotthis = 0;
    do
    {
        printf("before read_tx\n");
        if (read(fd, &byte, 1) <= 0)
        {
            printf("error\n");
            return -1;
        }
        printf("after read\n");
        printf("byte: %x\n", byte);
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
            if (byte == UA || byte == RR_0 || byte == RR_1 || byte == DISC || byte == REJ_0 || byte == REJ_1)
            {
                state = C_RCV;
                gotthis = byte;
                switch (byte)
                {
                case UA:
                    next_step = DO_NOTHING;
                    break;
                case RR_0:
                    next_step = SEND_DATA_0;
                    break;
                case RR_1:
                    next_step = SEND_DATA_1;
                    break;
                case DISC:
                    next_step = SEND_UA;
                    break;
                case REJ_0:
                    next_step = SEND_DATA_0;
                    break;
                case REJ_1:
                    next_step = SEND_DATA_1;
                    break;
                default:
                    break;
                }
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
            else if ((COMMAND_SENDER ^ gotthis) == byte)
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
                state = SUCCESS;
            }
            else
            {
                state = START;
            }
            break;
        default:
            break;
        }
    } while (state != SUCCESS);
    return 5;
}
int rec_read(unsigned char *packet)
{
    next_step = DO_NOTHING;
    state = START;
    unsigned int idx = 0, numBytesRead = 0;
    unsigned char byte = 0;
    unsigned char gotthis = 0;
    do
    {
        printf("before read\n");
        if (read(fd, &byte, 1) <= 0)
        {
            next_step = prev_step;
            return -1;
        }
        printf("after read\n");
        printf("byte: %x\n", byte);
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
            if (byte == SET || byte == DISC || byte == SD_0 || byte == SD_1)
            {
                state = C_RCV;
                gotthis = byte;
                switch (byte)
                {
                case SET:
                    next_step = SEND_UA;
                    break;
                case DISC:
                    next_step = SEND_DISC;
                    break;
                case SD_0:
                    prev_step = SEND_RR_0;
                    next_step = SEND_RR_1;
                    break;
                case SD_1:
                    prev_step = SEND_RR_1;
                    next_step = SEND_RR_0;
                    break;
                default:
                    break;
                }
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
            else if ((COMMAND_SENDER ^ gotthis) == byte)
            {
                state = BCC_OK;
            }
            else
            {
                state = START;
            }
            break;
        case BCC_OK:
            if (idx == 0 && byte == FLAG)
            {
                state = SUCCESS;
            }
            else if (idx != 0 && packet[idx - 1] != ESCAPE && byte == FLAG)
            {
                int size = 0;
                unstuffed_buffer = unstuff(packet, idx - 1, &size);
                if (idx != 0 && packet[idx - 1] == bcc2(unstuffed_buffer, size))
                {
                    idx--;
                    state = SUCCESS;
                    return size;
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
    return numBytesRead;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    unsigned char buf[] = {DISC};
    llwrite(buf, 1);
    if (next_step == SEND_UA)
    {
        buf[0] = UA;
        llwrite(buf, 1);
        return 1;
    }
    return -1;
}

int llread(unsigned char *packet)
{
    if (role == LlTx)
    {
        return trans_read();
    }
    if (role == LlRx)
    {
        return rec_read(packet);
    }
    return -1;
}
