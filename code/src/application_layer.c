// Application layer protocol implementation

#include <stdio.h>
#include "application_layer.h"
#include "link_layer.h"
#include "tmp.h"
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer connectionParameters;
    connectionParameters.baudRate = baudRate;
    connectionParameters.nRetransmissions = nTries;
    if (strcmp("tx", role) == 0)
    {
        connectionParameters.role = LlTx;
    }
    else
    {
        connectionParameters.role = LlRx;
    }
    connectionParameters.timeout = timeout;
    strcpy(connectionParameters.serialPort, serialPort);
    if (strcmp("tx", role) == 0)
    {
        if (llopen(connectionParameters) == -1)
        {
            printf("Couldn't open logical connection!\n");
            exit(-1);
        }
        int file = open(filename, O_RDONLY);
        unsigned char buffer[MAX_PAYLOAD_SIZE];
        unsigned int read_bytes = 0;
        while ((read_bytes = read(file, buffer, MAX_PAYLOAD_SIZE)) > 0)
        {
            printf("rb: %d\n", read_bytes);
            sendInformationFrame(buffer, read_bytes);
            printf("t2\n");
        }
        llclose(0);
        close(file);
    }
    else
    {
        llopen(connectionParameters);
        FILE *file = fopen(filename, "w");
        unsigned char *packet = NULL;
        unsigned int received_bytes = 0;
        while ((received_bytes = llread(packet)) != 0)
        {
            fwrite(packet, received_bytes, 1, file);
        }
        llclose(0);
        fclose(file);
    }
}
