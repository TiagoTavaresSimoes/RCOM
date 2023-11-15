#include "application_layer.h"
#include "link_layer.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>

#define BUF_SIZE 256
#define C_DADOS 0x01
#define C_START 0x02 
#define C_END 0x03

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename) {
    LinkLayer connectionParameters;
    int i = 0;
    while (serialPort[i] != '\0') {
        connectionParameters.serialPort[i] = serialPort[i];
        i++;
    }
    connectionParameters.serialPort[i] = serialPort[i];
    if (role[0] == 't') connectionParameters.role = LlTx;
    else if (role[0] == 'r') connectionParameters.role = LlRx;
    connectionParameters.baudRate = baudRate;
    connectionParameters.nRetransmissions = nTries;
    connectionParameters.timeout = timeout;

    if (llopen(connectionParameters) == -1) return;

    switch (connectionParameters.role) {
    case LlTx:
        sendFile(filename);
        break;
    case LlRx:
        receiveFile();
    default:
        break;
    }

    llclose(connectionParameters);
}

void sendFile(const char *filename) {
    FILE *file;
    file = fopen(filename, "rb");
    struct stat stats;
    stat(filename, &stats);
    size_t fileSize = stats.st_size;
    printf("File Size: %ld bytes \n", fileSize);
    printf("File Name: %s \n", filename);

    unsigned char start[30];
    start[0] = C_START;
    start[1] = 0x0;
    start[2] = sizeof(size_t);
    start[3] = (fileSize >> 24) & 0xFF;
    start[4] = (fileSize >> 16) & 0xFF;
    start[5] = (fileSize >> 8) & 0xFF;
    start[6] = fileSize & 0xFF;
    start[7] = strlen(filename)+1; 
    int j = 8;
    for(int k = 0; k < strlen(filename)+1; k++, j++) {
        start[j] = filename[k];
    }

    llwrite(start, j);
    printf("START MESSAGE SENT - %d bytes written \n", j);

    size_t bytesToSend = fileSize;
    unsigned int N = 0; 
    while (bytesToSend != 0) {
        unsigned char buffer[BUF_SIZE];
        size_t chunkSize = (bytesToSend >= BUF_SIZE - 4) ? BUF_SIZE - 4 : bytesToSend;
        buffer[0] = C_DADOS;
        buffer[1] = N % 255;
        buffer[2] = (chunkSize >> 8) & 0xFF;
        buffer[3] = chunkSize & 0xFF;
        fread(buffer + 4, 1, chunkSize, file);
        if (llwrite(buffer, chunkSize + 4) == -1) break;
        bytesToSend -= chunkSize;
        printf("DATA PACKET %d SENT - %ld bytes written (%ld bytes left) \n", N, chunkSize + 4, bytesToSend);
        N++;
    }

    unsigned char end[30];
    end[0] = C_END;
    for (int i = 1; i < 30; i++) {
        end[i] = start[i];
    }
    llwrite(end, j);
    printf("END MESSAGE SENT - %d bytes written \n", j);

    fclose(file);
}

void receiveFile() {
    unsigned char packet[BUF_SIZE];
    int bytes = llread(packet);
    if (bytes != -1) printf("START RECEIVED - %d bytes received \n", bytes);

    if (packet[0] != C_START) exit(-1);
    if (packet[1] != 0x0) exit(-1);
    size_t fileSize = (packet[3] << 24) | (packet[4] << 16) | (packet[5] << 8) | (packet[6]);

    char filename[20];
    for(int j = 0, i = 8; j < packet[7]; j++, i++) {
        filename[j] = packet[i];
    }

    printf("File Size: %ld bytes\n", fileSize);
    printf("File Name: %s \n", filename);

    unsigned char fileData[fileSize];
    unsigned int fileIndex = 0;
    while (TRUE) {
        unsigned char receivedData[BUF_SIZE];
        int bytesRead = llread(receivedData);

        if (bytesRead != -1) {
            if (receivedData[0] == C_DADOS) {
                printf("DATA PACKET RECEIVED - %d bytes received \n", bytesRead);
                unsigned int K = receivedData[2] * 256 + receivedData[3];
                for (int i = 4; i < K+4; i++, fileIndex++) {
                    fileData[fileIndex] = receivedData[i];
                }
            }

            if (receivedData[0] == C_END) {
                printf("END MESSAGE RECEIVED - %d bytes received \n", bytesRead);
                break;
            }
        }
    }

    unsigned char finalName[strlen(filename)+10];
    for (int i = 0, j = 0; i < strlen(filename)+10; i++, j++) {
        if (filename[j] == '.') {
            unsigned char received[10] = "-received";
            for (int k = 0; k < 9; k++, i++) {
                finalName[i] = received[k];
            }
        }
        finalName[i] = filename[j];
    }
    FILE *file;
    file = fopen((char *) finalName, "wb+");

    fwrite(fileData, sizeof(unsigned char), fileSize, file);

    fclose(file);
}
