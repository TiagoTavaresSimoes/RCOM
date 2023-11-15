// Link layer protocol implementation

#include "link_layer.h"
#include "unistd.h"
#include "signal.h"
#include "fcntl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>


// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source
struct termios oldtio;
struct termios newtio;

#define FLAG 0x7E
// Campo de Endereço:
#define A_SET 0x03    // Set de todos os comandos enviados pelo emissor e de todas as respostas retribuidas pelo receptor
#define A_UA 0x01    // Set de todos os comandos enviados pelo receptor e todas as respostas retribuidas pelo emissor

// Todo o campo de controlo:
// Definição do tipo de trama:
#define C_SET 0x03 
#define C_UA 0x07
#define C_DISC 0x0B
#define C_0 0x00
#define C_1 0x40
#define C_RR0 0x05
#define C_RR1 0x85
#define C_REJ 0x01

// Todo o campo de Proteção:
#define BCC_SET (A_SET ^ C_SET)
#define BCC_UA (A_UA ^ A_UA)
#define BUF_SIZE 256
#define BAUDRATE B9600 //teste da velocidade escolhida

// Função de Alarme semelhante à das aulas:

int fd;
int alarm_ligado = FALSE;
int alarm_count = 0;
int trama_0 = TRUE;
unsigned char trama_anterior[BUF_SIZE];

LinkLayer connection_parameters; 

// Função do Alarme:
void alarmHandler(int signal)
{
    alarm_ligado = FALSE;
    alarm_count++;

    printf("time-out %d\n", alarm_count);
    return;
}


////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////


int llopen(LinkLayer connectionParameters)
{
        printf("Começar LLOPEN------------------------------------\n");

    // guardar os connectionParameters para usar mais tarde:
    connection_parameters = connectionParameters;

    // abrir o serial port para ler e escrever
    fd = open(connectionParameters.serialPort, O_RDWR | O_NOCTTY);

    if (fd < 0)
    {
        perror(connectionParameters.serialPort);
        exit(-1);
    }

    // guardar as defirnições da current port
    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        exit(-1);
    }

    // limpar a struct para novas definições da porta
    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    // input mode (non-canonical, no echo,...)
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 1; 
    newtio.c_cc[VMIN] = 0; // bloqueio da leitura até receber 5 chars

    // o VTIME e o VMIN devem ser mudados para proteger a receção dos restantes caracteres (com o timeout)

    tcflush(fd, TCIOFLUSH);
    // tcflush() descarta todos os dados gravados no objeto referido pelo fd ou dados recebidos mas não lidos, despende.
    // depende do queue_selector
    // TCIFLUSH (argumento da tcflush) libera dados recebidos mas não lidos


    // nova definição das definições da porta
    if (tcsetattr(fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    printf("New termios structure set\n");

    // enviar/receber mensagens
    switch (connectionParameters.role)
    {

    // transmissor
    case LlTx:
        {
            // criar mensagem
            unsigned char set_message[BUF_SIZE];
            set_message[0] = FLAG;
            set_message[1] = A_SET;
            set_message[2] = C_SET;
            set_message[3] = BCC_SET;
            set_message[4] = FLAG;

            // chamar a função de alarme
            (void)signal(SIGALRM, alarmHandler);

            // enviar a mensagem no máximo 3 vezes
            while (alarm_count <= connectionParameters.nRetransmissions)
            {
                // enviar mensagem definida
                int bytes = write(fd, set_message, 5);
                printf("SET MESSAGE SENT - %d bytes written\n", bytes);

                // define alarme de 3 segundos
                if (alarm_ligado == FALSE)
                {
                    alarm(connectionParameters.timeout); // define o alarme para ser acionado em 3 segundos
                    alarm_ligado = TRUE;
                }

                // le a mensagem ua
                unsigned char buf[BUF_SIZE];
                int i = 0;
                int estado = 0;
                while (estado != 5)
                {
                    int bytes = read(fd, buf + i, 1);
                    if (bytes == -1) break;
                    if (bytes > 0) {
                        // máquina de estados semelhante à das aulas
                        switch (estado)
                        {
                        case 0:
                            if (buf[i] == FLAG) estado = 1;
                            break;
                        case 1:
                            if (buf[i] == FLAG) estado = 1;
                            if (buf[i] == A_UA) estado = 2;
                            else estado = 0;
                            break;
                        case 2:
                            if (buf[i] == FLAG) estado = 1;
                            if (buf[i] == C_UA) estado = 3;
                            else estado = 0;
                            break;
                        case 3:
                            if (buf[i] == FLAG) estado = 1;
                            if (buf[i] == BCC_UA) estado = 4;
                            else {
                                printf("error\n");
                                estado = 0;
                            }
                            break;
                        case 4:
                            if (buf[i] == FLAG) estado = 5;
                            else estado = 0;
                            break;
                        
                        default:
                            break;
                        }
                        i++; 
                    }
                    // timeout
                    if (alarm_ligado == FALSE) break;
                }
                
                // mensagem ua recebida
                if (estado == 5) {
                    alarm_count = 0;
                    printf("UA RECEIVED\n");
                    break;
                }
            }
        }
        break;
    // recetor
    case LlRx:
        {
            // le a mensagem recebida
            unsigned char buf[BUF_SIZE];
            int i = 0;
            int estado = 0;
            while (estado != 5)
            {
                int bytes = read(fd, buf + i, 1);
                if (bytes > 0) {
                    // máquina de estados
                    switch (estado)
                    {
                    case 0:
                        if (buf[i] == FLAG) estado = 1;
                        break;
                    case 1:
                        if (buf[i] == A_SET) estado = 2;
                        else estado = 0;
                        break;
                    case 2:
                        if (buf[i] == FLAG) estado = 1;
                        if (buf[i] == C_SET) estado = 3;
                        else estado = 0;
                        break;
                    case 3:
                        if (buf[i] == FLAG) estado = 1;
                        if (buf[i] == BCC_SET) estado = 4;
                        else {
                            printf("error\n");
                            estado = 0;
                        }
                        break;
                    case 4:
                        if (buf[i] == FLAG) estado = 5;
                        else estado = 0;
                        break;
                    
                    default:
                        break;
                    }
                    i++; 
                }
            }
            printf("SET RECEIVED\n");

            // envia a mensagem
            unsigned char ua_message[BUF_SIZE];
            ua_message[0] = FLAG;
            ua_message[1] = A_UA;
            ua_message[2] = C_UA;
            ua_message[3] = BCC_UA;
            ua_message[4] = FLAG;

            int bytes = write(fd, ua_message, 5);
            printf("UA MESSAGE SENT - %d bytes written\n", bytes);
        }
        break;
    default:
        break;
    }
    printf("Fim do LLOPEN ---------------------------------------\n");

    // erro de timeout:
    if (alarm_count > connection_parameters.nRetransmissions) {
        alarm_count = 0;
        return -1;
    }

    // bem sucedido:
    return 1;
}



////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////


int llwrite(const unsigned char *buf, int bufSize)
{
    printf("Começar LLWRITE ---------------------------------------\n");
    printf("Envio da trama %d\n", !trama_0);

    // cria BCC2
    unsigned char BCC2 = buf[0];
    for (int i = 1; i < bufSize; i++) {
        BCC2 ^= buf[i];
    }

    // BYTE STUFFING 
    int contador = 0;
    for (int i = 0; i < bufSize; i++) {
        // descobre quantos 0x7E ou 0x7D estão presentes no buf
        if (buf[i] == 0x7E || buf[i] == 0x7D) contador++;
    }

    // verificar bcc2
    if (BCC2 == 0x7E || BCC2 == 0x7D) contador++;
    unsigned char buf_apos_stuffing[bufSize + contador];

    // dar replace ao 0x7E e 0x7D
    for (int i = 0, j = 0; i < bufSize; i++) {
        if (buf[i] == 0x7E) {
            buf_apos_stuffing[j] = 0x7D;
            buf_apos_stuffing[j+1] = 0x5E;
            j+=2;
        }
        else if (buf[i] == 0x7D) {
            buf_apos_stuffing[j] = 0x7D;
            buf_apos_stuffing[j+1] = 0x5D;
            j+=2;
        }
        else {
            buf_apos_stuffing[j] = buf[i];
            j++;
        }
    }

    // criar a mensagem de informação
    unsigned char packet_enviar[bufSize + contador+6];
    packet_enviar[0] = FLAG;
    packet_enviar[1] = A_SET;
    switch (trama_0)
    {
    case TRUE:
        packet_enviar[2] = C_0;
        break;
    case FALSE:
        packet_enviar[2] = C_1;
        break;
    default:
        break;
    }
    packet_enviar[3] = packet_enviar[1] ^ packet_enviar[2];

    // adicionar o buf à mensagem de informação
    for (int i = 4, j = 0; i < bufSize + contador + 4; i++, j++) {
        packet_enviar[i] = buf_apos_stuffing[j];
    }
    // byte stuffing para o bcc2
    if (BCC2 == 0x7E) {
        packet_enviar[bufSize + contador + 3] = 0x7D;
        packet_enviar[bufSize + contador + 4] = 0x5E;
        packet_enviar[bufSize + contador + 5] = FLAG;
    }
    else if (BCC2 == 0x7D) {
        packet_enviar[bufSize + contador + 3] = 0x7D;
        packet_enviar[bufSize + contador + 4] = 0x5D;
        packet_enviar[bufSize + contador + 5] = FLAG;
    }
    else {
        packet_enviar[bufSize + contador + 4] = BCC2;
        packet_enviar[bufSize + contador + 5] = FLAG;
    }

    // próxima trama para enviar
    if (trama_0 == TRUE) trama_0 = FALSE;
    else trama_0 = TRUE;

    // enviar mensagem de informação:

    // definição da função de alarme
    (void)signal(SIGALRM, alarmHandler);

    // envia a mensagem no máximo 3 vezes
    while (alarm_count <= connection_parameters.nRetransmissions)
    {
        int bytes = write(fd, packet_enviar, sizeof(packet_enviar));
        printf("INFORMATION PACKET SENT - %d bytes written\n", bytes);

        // define um alarme de 3 segundos
        if (alarm_ligado == FALSE)
        {
            alarm(connection_parameters.timeout); // alarme para ser acionado em 3 segundos
            alarm_ligado = TRUE;
        }

        // resposta de leitura
        unsigned char resposta[BUF_SIZE];
        int i = 0;
        int estado = 0;
        while (estado != 5)
        {
            int bytes = read(fd, resposta + i, 1);
            if (bytes == -1) break;
            if (bytes > 0) {
                // máquina de estados novamente
                switch (estado)
                {
                case 0:
                    if (resposta[i] == FLAG) estado = 1;
                    break;
                case 1:
                    if (resposta[i] == FLAG) estado = 1;
                    if (resposta[i] == A_UA) estado = 2;
                    else estado = 0;
                    break;
                case 2:
                    if (resposta[i] == FLAG) estado = 1;
                    if (resposta[i] == C_REJ) {
                        estado = 0;
                        printf("REJ received\n");
                        break;
                    }
                    if (trama_0 == TRUE && resposta[i] == C_RR0) estado = 3;
                    else if (trama_0 == FALSE && resposta[i] == C_RR1) estado = 3;
                    else {
                        // envio da trama anterior
                        int bytes = write(fd, trama_anterior, sizeof(trama_anterior));
                        printf("INFORMATION PACKET SENT - %d bytes written\n", bytes);
                        estado = 0;
                    }
                    break;
                case 3:
                    if (resposta[i] == FLAG) estado = 1;
                    if (resposta[i] == (resposta[i-1] ^ resposta[i-2])) estado = 4;
                    else estado = 0;
                    break;
                case 4:
                    if (resposta[i] == FLAG) estado = 5;
                    else estado = 0;
                    break;
                
                default:
                    break;
                }
                i++; 
            }
            // timeout
            if (alarm_ligado == FALSE) break;
        }
        
        // mensagem rr recebida
        if (estado == 5) {
            alarm_count = 0;
            printf("RR RECEIVED\n");
            break;
        }
    }

    // guardar a trama enviada
    for (int i = 0; i < BUF_SIZE; i++) {
        trama_anterior[i] = packet_enviar[i];
    }

    printf("Fim do LLWRITE ---------------------------------------\n");

    // erro de timeout:
    if (alarm_count > connection_parameters.nRetransmissions) {
        alarm_count = 0;
        return -1;
    }

    // Sbem sucedido:
    return sizeof(packet_enviar);

    return 0;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    printf("Começar LLREAD ---------------------------------------\n");
    printf("Receber trama %d\n", !trama_0);

    // leitura da informação
    unsigned char buf[BUF_SIZE];
    int i = 0, tamanho_buf = 0, error = FALSE;
    int estado = 0;
    while (estado != 5)
    {
        int bytes = read(fd, buf + i, 1);
        if (bytes > 0) {
            // máquina de estados
            switch (estado)
            {
            case 0:
                if (buf[i] == FLAG) estado = 1;
                break;
            case 1:
                if (buf[i] == A_SET) estado = 2;
                else estado = 0;
                break;
            case 2:
                if (buf[i] == FLAG) estado = 1;
                if (trama_0 == TRUE && buf[i] == C_0) estado = 3;
                else if (trama_0 == FALSE && buf[i] == C_1) estado = 3;

                // trama 1 não esperada:
                else if (trama_0 == TRUE && buf[i] == C_1) {
                    estado = 5;
                    error = TRUE;
                }

                // trama 0 não esperada:
                else if (trama_0 == TRUE && buf[i] == C_0) {
                    estado = 5;
                    error = TRUE;
                }
                else estado = 0;
                break;
            case 3:
                if (buf[i] == FLAG) estado = 1;
                if (buf[i] == (buf[i-1] ^ buf[i-2])) {
                    estado = 4;
                    i = -1;
                }
                else {
                    estado = 0;
                    printf("error\n");
                }
                break;
            case 4:
                if (buf[i] == FLAG) estado = 5;
                else {
                    tamanho_buf++;
                }
                break;
            
            default:
                break;
            }
            i++; 
        }
    }

    // BYTE DESTUFFING 
    int tamanho_packet = tamanho_buf;
    for (int i = 0, j = 0; i < tamanho_buf; i++, j++) {
        if (buf[i] == 0x7D && buf[i+1] == 0x5E) {
            packet[j] = 0x7E;
            i++; tamanho_packet--;
        }
        else if (buf[i] == 0x7D && buf[i+1] == 0x5D) {
            packet[j] = 0x7D;
            i++; tamanho_packet--;
        }
        else {
            packet[j] = buf[i];
        }
    }

    // verificar BCC2
    tamanho_packet--;
    unsigned char BCC2 = packet[0];
    for (int i = 1; i < tamanho_packet; i++) {
        BCC2 ^= packet[i];
    }

    printf("PACKET RECEIVED\n");

    // criar a mensagem rr
    unsigned char rr_message[5];
    rr_message[0] = FLAG;
    rr_message[1] = A_UA;
    // erro BCC
    if (BCC2 != packet[tamanho_packet]) {
        printf("error in the data\n");
        // enviar REJ
        rr_message[2] = C_REJ;
        rr_message[3] = rr_message[1] ^ rr_message[2];
        rr_message[4] = FLAG;

        int bytes = write(fd, rr_message, 5);
        printf("REJ MESSAGE SENT - %d bytes written\n", bytes);

        printf("Fim do LLREAD ---------------------------------------\n");
        return -1;
    }
    if (trama_0 == TRUE) {
        // frame duplicado
        if (error) {
            rr_message[2] = C_RR0;
            printf("duplicate frame \n");
        }
        // sem erro
        else {
            rr_message[2] = C_RR1;
            trama_0 = FALSE;
        }
    }
    else {
        // frame duplicado
        if (error) {
            rr_message[2] = C_RR1;
            printf("duplicate frame \n");
        }
        // sem erro
        else {
            rr_message[2] = C_RR0;
            trama_0 = TRUE;
        }
    }
    rr_message[3] = rr_message[1] ^ rr_message[2];
    rr_message[4] = FLAG;

    // enviar a mensagem rr
    int bytes = write(fd, rr_message, 5);
    printf("RR MESSAGE SENT - %d bytes written\n", bytes);

    printf("Fim do LLREAD ---------------------------------------\n");

    // erro por se receber uma trama duplicada
    if (error) return -1;

    // bem sucedido
    return tamanho_packet;

    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////

int llclose(LinkLayer connectionParameters)
{
    printf("Começar LLCLOSE ---------------------------------------\n");


    // enviar/receber mensagens
    switch (connection_parameters.role)
    {
    // transmissor:
    case LlTx:
        {
            // criar a mensagem disc
            unsigned char disc_message[BUF_SIZE];
            disc_message[0] = FLAG;
            disc_message[1] = A_SET;
            disc_message[2] = C_DISC;
            disc_message[3] = (A_SET ^ C_DISC);
            disc_message[4] = FLAG;

            // definir a função do alarme
            (void)signal(SIGALRM, alarmHandler);

            // enviar a mensagem no máximo 3 vezes
            while (alarm_count <= connection_parameters.nRetransmissions)
            {
                // enviar a mensagem disc
                int bytes = write(fd, disc_message, 5);
                printf("DISC MESSAGE SENT - %d bytes written\n", bytes);

                // define o alarme de 3 segundos
                if (alarm_ligado == FALSE)
                {
                    alarm(connection_parameters.timeout); // alarme para ser acionado em 3 segundos
                    alarm_ligado = TRUE;
                }

                // ler a mensagem disc
                unsigned char buf[BUF_SIZE];
                int i = 0;
                int estado = 0;
                while (estado != 5)
                {
                    int bytes = read(fd, buf + i, 1);
                    if (bytes == -1) break;
                    if (bytes > 0) {
                        // máquina de estados
                        switch (estado)
                        {
                        case 0:
                            if (buf[i] == FLAG) estado = 1;
                            break;
                        case 1:
                            if (buf[i] == FLAG) estado = 1;
                            if (buf[i] == A_UA) estado = 2;
                            else estado = 0;
                            break;
                        case 2:
                            if (buf[i] == FLAG) estado = 1;
                            if (buf[i] == C_DISC) estado = 3;
                            else estado = 0;
                            break;
                        case 3:
                            if (buf[i] == FLAG) estado = 1;
                            if (buf[i] == (A_UA ^ C_DISC)) estado = 4;
                            else {
                                printf("error\n");
                                estado = 0;
                            }
                            break;
                        case 4:
                            if (buf[i] == FLAG) estado = 5;
                            else estado = 0;
                            break;
                        
                        default:
                            break;
                        }
                        i++; 
                    }
                    // timeout
                    if (alarm_ligado == FALSE) break;
                }
                
                // mensagem disc recebida
                if (estado == 5) {
                    alarm_count = 0;
                    printf("DISC RECEIVED\n");
                    break;
                }
            }

            // enviar a mensagem ua
            unsigned char ua_message[BUF_SIZE];
            ua_message[0] = FLAG;
            ua_message[1] = A_UA;
            ua_message[2] = C_UA;
            ua_message[3] = BCC_UA;
            ua_message[4] = FLAG;

            int bytes = write(fd, ua_message, 5);
            printf("UA MESSAGE SENT - %d bytes written\n", bytes);
        }
        break;
    // recetor:
    case LlRx:
        {
            // ler a mensagem disc
            unsigned char buf[BUF_SIZE];
            int i = 0;
            int estado = 0;
            while (estado != 5)
            {
                int bytes = read(fd, buf + i, 1);
                if (bytes > 0) {
                    // máquina de estados
                    switch (estado)
                    {
                    case 0:
                        if (buf[i] == FLAG) estado = 1;
                        break;
                    case 1:
                        if (buf[i] == A_SET) estado = 2;
                        else estado = 0;
                        break;
                    case 2:
                        if (buf[i] == FLAG) estado = 1;
                        if (buf[i] == C_DISC) estado = 3;
                        else estado = 0;
                        break;
                    case 3:
                        if (buf[i] == FLAG) estado = 1;
                        if (buf[i] == (A_SET ^ C_DISC)) estado = 4;
                        else {
                            printf("error\n");
                            estado = 0;
                        }
                        break;
                    case 4:
                        if (buf[i] == FLAG) estado = 5;
                        else estado = 0;
                        break;
                    
                    default:
                        break;
                    }
                    i++; 
                }
            }
            printf("DISC RECEIVED\n");

            // enviar mensagem disc
            unsigned char disc_message[BUF_SIZE];
            disc_message[0] = FLAG;
            disc_message[1] = A_UA;
            disc_message[2] = C_DISC;
            disc_message[3] = (A_UA ^ C_DISC);
            disc_message[4] = FLAG;

            int bytes = write(fd, disc_message, 5);
            printf("DISC MESSAGE SENT - %d bytes written\n", bytes);

            // ler mensagem ua
            i = 0;
            estado = 0;
            while (estado != 5)
            {
                int bytes = read(fd, buf + i, 1);
                if (bytes == -1) break;
                if (bytes > 0) {
                    // máquina de estados
                    switch (estado)
                    {
                    case 0:
                        if (buf[i] == FLAG) estado = 1;
                        break;
                    case 1:
                        if (buf[i] == FLAG) estado = 1;
                        if (buf[i] == A_UA) estado = 2;
                        else estado = 0;
                        break;
                    case 2:
                        if (buf[i] == FLAG) estado = 1;
                        if (buf[i] == C_UA) estado = 3;
                        else estado = 0;
                        break;
                    case 3:
                        if (buf[i] == FLAG) estado = 1;
                        if (buf[i] == BCC_UA) estado = 4;
                        else {
                            printf("error\n");
                            estado = 0;
                        }
                        break;
                    case 4:
                        if (buf[i] == FLAG) estado = 5;
                        else estado = 0;
                        break;
                    
                    default:
                        break;
                    }
                    i++; 
                }
            }
            printf("UA RECEIVED\n");
        }
        break;
    default:
        break;
    }

    // redifinir as definições antigas da porta
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    close(fd);

    printf("Fim do LLCLOSE ---------------------------------------\n");

    return 1;
}
