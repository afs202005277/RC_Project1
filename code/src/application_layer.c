// Application layer protocol implementation

#include <stdio.h>
#include "application_layer.h"
#include "link_layer.h"
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer connectionParameters;
    connectionParameters.baudRate = baudRate;
    connectionParameters.nRetransmissions = nTries;
    if (strcmp("tx", role) == 0){
        connectionParameters.role = LlTx;
    } else {
        connectionParameters.role = LlRx;
    }
    connectionParameters.timeout = timeout;
    strcpy(connectionParameters.serialPort, serialPort);
    if (strcmp("tx", role) == 0)
    {
        if (llopen(connectionParameters) == -1){
            printf("Couldn't open logical connection!\n");
            exit(-1);
        }
        FILE *file = fopen(filename, "r");
        unsigned char buffer[MAX_PAYLOAD_SIZE];
        unsigned int read_bytes = 0;
        while ((read_bytes = fread(buffer, MAX_PAYLOAD_SIZE, 1, file)) > 0)
        {
            llwrite(buffer, read_bytes);
        }
        llclose(0);
    }
    else
    {
        llopen(connectionParameters);
        FILE *file = fopen(filename, "w");
        unsigned char* packet=NULL;
        unsigned int received_bytes = 0;
        while((received_bytes = llread(packet)) != 0){
            fwrite(packet, received_bytes, 1, file);
        }
    }
}
