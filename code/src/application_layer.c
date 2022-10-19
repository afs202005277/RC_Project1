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
        /*int file = open(filename, O_RDONLY);
        unsigned char buffer[MAX_PAYLOAD_SIZE];
        unsigned int read_bytes = read(file, buffer, MAX_PAYLOAD_SIZE);
        sendInformationFrame(buffer, read_bytes);*/
        /*while ((read_bytes = read(file, buffer, MAX_PAYLOAD_SIZE)) > 0)
        {
            sendInformationFrame(buffer, read_bytes);
            break;
        }*/
        //close(file);
        //llclose(0);
    }
    else
    {
        llopen(connectionParameters);
        int file = open(filename, O_WRONLY | O_CREAT);
        unsigned char *packet = malloc(3*MAX_PAYLOAD_SIZE);
        unsigned int received_bytes = 0;
        while ((received_bytes = getInformationFrame(packet)) != 0)
        {
            printf("%d\n", received_bytes);
            //printBuffer(packet, received_bytes);
            write(file, packet, received_bytes);
            break;
        }
        close(file);
        llclose(0);
    }
}
