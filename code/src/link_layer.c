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
        newtio.c_cc[VTIME] = 40;
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
            res = sendInformationFrame(buf, bufSize);
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
            res = sendInformationFrame(buf, bufSize);
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
        //res = llwrite(rr1, 5);
        res = write(fd, rr1, 5);
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
    return llwrite(frame, stuffedSize + 6);
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
    unsigned char *answer = NULL;
    attempts = 0;
    printf("SENDING1\n");
    if (write(fd, buf, bufSize) < 0)
    {
        printf("fd: %d\n", fd);
        printf("bufSize: %d\nbuf: ", bufSize);
        printBuffer(buf, bufSize);
        perror("Couldn't write to the serial port: ");
        return -1;
    }
    if (role == LlTx)
    {
        printBuffer(buf, bufSize);
        while (attempts < maxRepeat)
        {
            printf("%d\n", attempts);
            //alarm(timeout);
            printf("timeout: %d\n", timeout);
            attempts++;
            if (alarmTriggered == TRUE)
            {
                printf("SENDING3\n");
                if (write(fd, buf, bufSize) != bufSize)
                {
                    perror("Couldn't write to the serial port: ");
                    continue;
                }
            }
            llread(answer);
            handleNextStep(buf, bufSize);
            if (state == SUCCESS)
            {
                if (connection_state == ESTABLISHMENT)
                {
                    connection_state = DATA_TRANSFER;
                    printf("Logical connection established successfully!\n");
                }
                printf("DEACTIVATE ALARM\n");
                //alarm(0);
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
int getInformationFrame(unsigned char *buf)
{
    unsigned char *tmp = malloc(MAX_BYTES);
    unstuffed_buffer = malloc(MAX_BYTES);
    int num_bytes = llread(tmp);
    memcpy(buf, unstuffed_buffer, num_bytes);
    return num_bytes;
}

int llread(unsigned char *packet)
{
    if (role == LlTx) {
        return trans_read(packet);
    }
    if (role == LlRx) {
        return rec_read(packet);
    }
    return -1;
}

int trans_read(unsigned char *packet) {
    next_step = DO_NOTHING;
    state = START;
    unsigned int idx = 0, numBytesRead = 0;
    unsigned char byte = 0;
    do
    {
        if (read(fd, &byte, 1) <= 0)
            break;
        numBytesRead++;
        printf("byte_read: %x\n", byte);
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
            else if (byte == COMMAND_RECEIVER && connection_state == TERMINATION)
            {
                state = A_RCV;
                next_step = SEND_UA;
            }
            break;
        case A_RCV:
            if (role == LlTx && byte == UA)
            {
                state = C_RCV;
                next_step = DO_NOTHING;
            }
            else if (byte == FLAG)
            {
                state = FLAG_RCV;
            }
            else if (byte == DISC)
            {
                state = C_RCV;
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
            else if (byte == (COMMAND_RECEIVER ^ DISC) && connection_state == TERMINATION)
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
            state = SUCCESS;
            break;
        default:
            break;
        }
    } while (state != SUCCESS);
    return numBytesRead;
}

int rec_read(unsigned char *packet) {
    next_step = DO_NOTHING;
    state = START;
    unsigned int idx = 0, numBytesRead = 0;
    unsigned char byte = 0;
    do
    {
        if (read(fd, &byte, 1) <= 0)
            break;
        numBytesRead++;
        printf("byte_read: %x\n", byte);
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
                next_step = DO_NOTHING;
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
            else if (byte == (COMMAND_RECEIVER ^ DISC) && role == LlTx && connection_state == TERMINATION)
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
