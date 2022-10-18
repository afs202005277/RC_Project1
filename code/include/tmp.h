int sendInformationFrame(const unsigned char *buf, int bufsize);
int getInformationFrame(unsigned char *buf);
int handleNextStep(const unsigned char *buf, int bufSize);
void printBuffer(const unsigned char *buf, int bufsize);