// Link layer protocol implementation

#include "link_layer.h"

TO DO:
- adicionar read
- funcao sendFrame e receiveFrame
- Protocolo de ligacao de dados
- fazer disconnect

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////


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

unsigned char alarmTriggered = FALSE;
unsigned char attempts = 0;

void alarmHandler(int num)
{
    alarmTriggered = TRUE;
    printf("Attempt #%d failed!\n", attempts);
}

void readPackage(int fd, unsigned char *buffer)
{
    unsigned char byte = 0;
    do
    {
        if (read(fd, &byte, 1) == 0)
            break;
        printf("%x\n", byte);
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
            if (byte == UA)
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
            else if ((COMMAND_SENDER ^ UA) == byte)
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

int makeConnection(int fd)
{
    unsigned char buf[BUF_SIZE] = {0};
    unsigned char setUp[5];
    setUp[0] = FLAG;
    setUp[1] = COMMAND_SENDER;
    setUp[2] = SET;
    setUp[3] =COMMAND_SENDER ^ SET;
    setUp[4] = FLAG;
    // unsigned char uaReceive[] = {FLAG, COMMAND_SENDER, UA, COMMAND_SENDER ^ UA, FLAG};
    if (write(fd, setUp, SIZE_COMMAND_WEBS) == -1)
    {
        perror("Couldn't write to the serial port: ");
    }
    while (attempts < MAX_REPEAT)
    {
        attempts++;
        if (alarmTriggered == TRUE)
            if (write(fd, setUp, SIZE_COMMAND_WEBS) == -1)
            {
                perror("Couldn't write to the serial port: ");
            }
        alarm(3);

        readPackage(fd, buf);
        if (state == STOP)
        {
            printf("Logical connection established successfully!\n");
            alarm(0);
            return attempts;
        }
    }
    printf("Giving up...\n");
    return attempts;
}


int llopen(LinkLayer connectionParameters)
{
    int fd = open(serialPortName, O_RDWR | O_NOCTTY);

    if (fd < 0)
    {
        perror(serialPortName);
        exit(-1);
    }

    struct termios oldtio;
    struct termios newtio;

    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        exit(-1);
    }

    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 30;
    newtio.c_cc[VMIN] = 0;

    tcflush(fd, TCIOFLUSH);

    if (tcsetattr(fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    printf("New termios structure set\n");

    (void)signal(SIGALRM, alarmHandler);

    makeConnection(fd);

    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    return 0;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize)
{
    // TODO

    return 0;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    // TODO

    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    // mandar disc
    close(fd)

    return 1;
}
