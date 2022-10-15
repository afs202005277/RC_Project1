// Application layer protocol implementation

#include <stdio.h>
#include "application_layer.h"
#include "link_layer.h"

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer connectionParameters = {serialPort, role, baudRate, nTries, timeout};
    llopen(connectionParameters);
    FILE* file = fopen(filename, "r");
    unsigned char* buffer[MAX_PAYLOAD_SIZE];
    unsigned int read_bytes = 0;
    while(read_bytes = fread(buffer, MAX_PAYLOAD_SIZE, 1, file)){
        llwrite(buffer, read_bytes);
    }
    llclose(0);
}
