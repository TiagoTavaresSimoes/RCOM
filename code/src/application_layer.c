// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>

#define BUF_SIZE 256 //define o tamanho do buf como 256
// Campo de Controlo
// Define o tipo de trama 
#define C_DADOS 0x01
#define C_START 0x02 
#define C_END 0x03

// Implementar a função principal que cordena a transferência de arquivos:

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    // ir buscar os connection parameters
    LinkLayer connectionParameters;
    int i = 0;
    while (serialPort[i] != '\0')
    {
        connectionParameters.serialPort[i] = serialPort[i];
        i++;
    }
    connectionParameters.serialPort[i] = serialPort[i];
    if (role[0] == 't') connectionParameters.role = LlTx;
    else if (role[0] == 'r') connectionParameters.role = LlRx;
    connectionParameters.baudRate = baudRate;
    connectionParameters.nRetransmissions = nTries;
    connectionParameters.timeout = timeout;

    // chamada do llopen do link layer
    if (llopen(connectionParameters) == -1) return; //llopen é chamada para estabelecer a conexão

    switch (connectionParameters.role)
    {
    case LlTx:
        // envia o ficheiro
        {
            sendFile(filename);
        }
        break;
    case LlRx:
        // recebe o ficheiro
        {
            receiveFile();
        }
    default:
        break;
    }

    // chamada do llclose do link layer
    llclose(connectionParameters); //llclose é chamada para encerrar a conexão
}

//função responsável pelo envio do ficheiro:

void sendFile(const char *filename) {
    // abrir o ficheiro para leitura
    FILE *file;
    file = fopen(filename, "rb");

    // buscar o tamanho
    struct stat stats;
    stat(filename, &stats);
    size_t tamanho_ficheiro = stats.st_size;
    printf("File Size: %ld bytes \n", tamanho_ficheiro);
    printf("File Name: %s \n", filename);

    // leitura do ficheiro
    unsigned char *dados_ficheiro;
    dados_ficheiro = (unsigned char *)malloc(tamanho_ficheiro);
    fread(dados_ficheiro, sizeof(unsigned char), tamanho_ficheiro, file);

    // start da trama
    // envia a trama de start para obter o tamanho do arquivo e o nome
    unsigned char start[30];
    start[0] = C_START;

    // faz do zero até ao tamanho do ficheiro
    start[1] = 0x0;
    start[2] = sizeof(size_t);
    start[3] = (tamanho_ficheiro >> 24) & 0xFF;
    start[4] = (tamanho_ficheiro >> 16) & 0xFF;
    start[5] = (tamanho_ficheiro >> 8) & 0xFF;
    start[6] = tamanho_ficheiro & 0xFF;
    
    // vê do um até ao nome do ficheiro
    start[7] = strlen(filename)+1; 
    int i = 8;
    for(int j = 0; j < strlen(filename)+1; j++, i++) {
        start[i] = filename[j];
    }

    llwrite(start, i);
    printf("START MESSAGE SENT - %d bytes written \n", i);

    // envio da data do ficheiro
    // o ficheiro vai ser divido em pacotes de dados (data packets) e vão ser enviados:
    size_t bytes_enviar = tamanho_ficheiro;
    unsigned int N = 0; unsigned int indice_ficheiro = 0;
    while (bytes_enviar != 0)
    {
        if (bytes_enviar >= 100) {
            unsigned char data_packet[104]; // enviar 100 bytes
            data_packet[0] = C_DADOS;
            data_packet[1] = N % 255; // N – número de sequência (módulo 255)
            data_packet[2] = 0x0; // L2 L1 – indica o número de octetos (K) do campo de dados
            data_packet[3] = 0x64; // (K = 256 * L2 + L1)
            for (int i = 4; i < 104; i++, indice_ficheiro++) {
                data_packet[i] = dados_ficheiro[indice_ficheiro];
            }
            if (llwrite(data_packet, 104) == -1) break;
            bytes_enviar-=100;
            printf("DATA PACKET %d SENT - %d bytes written (%ld bytes left) \n", N, 104, bytes_enviar);
        }
        else {
            unsigned char data_packet[bytes_enviar+4]; // enviar 100 bytes
            data_packet[0] = C_DADOS;
            data_packet[1] = N % 255; // N – número de sequência (módulo 255)
            data_packet[2] = 0x0; // L2 L1 – indica o número de octetos (K) do campo de dados
            data_packet[3] = bytes_enviar; // (K = 256 * L2 + L1)
            for (int i = 4; i < bytes_enviar + 4; i++, indice_ficheiro++) {
                data_packet[i] = dados_ficheiro[indice_ficheiro];
            }
            if (llwrite(data_packet, bytes_enviar+4) == -1) break;
            printf("DATA PACKET %d SENT - %ld bytes written (%d bytes left) \n", N, bytes_enviar, 0);
            bytes_enviar = 0;
        }
        N++;
    }

    // avisa do final da trama
    // esta trama é enviada para avisar o final da transferência
    unsigned char end[30];
    end[0] = C_END;
    for (int i = 1; i < 30; i++) {
        end[i] = start[i];
    }
    llwrite(end, i);
    printf("END MESSAGE SENT - %d bytes written \n", i);

    // fechar o ficheiro para leitura
    fclose(file);
}

// função responsável por receber o ficheiro:
void receiveFile() {
    // recebe a trama de inicio para obter o tamanho do ficheiro e o nome
    unsigned char packet[BUF_SIZE];
    int bytes = llread(packet);
    if (bytes != -1) printf("START RECEIVED - %d bytes received \n", bytes);

    // verificar o valor
    if (packet[0] != C_START) exit(-1);

    // buscar o tamanho do ficheiro
    if (packet[1] != 0x0) exit(-1);
    size_t tamanho_ficheiro = (packet[3] << 24) | (packet[4] << 16) | (packet[5] << 8) | (packet[6]);

    // buscar o nome do ficheiro 
    char filename[20];
    for(int j = 0, i = 8; j < packet[7]; j++, i++) {
        filename[j] = packet[i];
    }

    printf("File Size: %ld bytes\n", tamanho_ficheiro);
    printf("File Name: %s \n", filename);

    // recebe até ao final da trama
    unsigned char dados_ficheiro[tamanho_ficheiro];
    unsigned int indice_ficheiro = 0;
    while (TRUE)
    {
        unsigned char dados_recebidos[BUF_SIZE];
        int bytes = llread(dados_recebidos);

        if (bytes != -1) {
            if (dados_recebidos[0] == C_DADOS) {
                if (bytes != -1) printf("DATA PACKET RECEIVED - %d bytes received \n", bytes);
                // buscar o k
                unsigned int K = dados_recebidos[2] * 256 + dados_recebidos[3];
                for (int i = 4; i < K+4; i++, indice_ficheiro++) {
                    dados_ficheiro[indice_ficheiro] = dados_recebidos[i];
                }
            }

            if (dados_recebidos[0] == C_END) {
                if (bytes != -1) printf("END MESSAGE RECEIVED - %d bytes received \n", bytes);
                break;
            }
        }
    }

    // guardar o ficheiro
    // buscar o nome do ficheiro recebido
    unsigned char nome_final[strlen(filename)+10];
    for (int i = 0, j = 0; i < strlen(filename)+10; i++, j++) {
        if (filename[j] == '.') {
            unsigned char received[10] = "-received";
            for (int k = 0; k < 9; k++, i++) {
                nome_final[i] = received[k];
            }
        }
        nome_final[i] = filename[j];
    }
    // abrir o ficheiro para escrita
    FILE *file;
    file = fopen((char *) nome_final, "wb+");

    // escrever toda a data (dados) no ficheiro
    fwrite(dados_ficheiro, sizeof(unsigned char), tamanho_ficheiro, file);

    // fechar o ficheiro para escrita
    fclose(file);
}


