#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma GCC diagnostic ignored "-Wunused-variable"

#include "linklayer.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>

#define BAUDRATE 9600 //38400?
#define MODEMDEVICE "/dev/ttyS1"
#define _POSIX_SOURCE 1 /* POSIX compliant source */
#define FALSE 0
#define TRUE 1
#define MAXR 3 //no maximo de falhas
#define MAXT 1 //valor do temporizador
#define MAX_SIZE 256 //Tamanho maximo de uma frame APÓS stuffing


#define FLAG 0x7E
#define ESC 0x7D
#define STFF 0x20
#define SET 0x03
#define DISC 0x0B
#define UA 0x07 
#define RR 0x05
#define REJ 0x01
#define A 0x03 //Comando enviado pelo emissor e resposta enviada pelo receptor

#define RECEIVER 0
#define TRANSMITTER 1

#define DATA_RECEIVED 0
#define SET_RECEIVED 1
#define DISC_RECEIVED 2
#define UA_RECEIVED 3
#define RR_RECEIVED 4
#define REJ_RECEIVED 5

#define DEBUG 1


int fd, txrx, N;

volatile int STOP=FALSE;
int conta = 0;
int alarm_flag = 1;

struct linkLayerStruct{
	
	char port[20];
	int fd;
	unsigned int sequenceNumber;
	unsigned int timeout;//valor do temporizador
	int frameLength;
	unsigned char frame[((MAX_SIZE*2)+6)];
	unsigned int numTransmissions;
	int State ;//Transmitter Reciver
	
}linkLayer;



int config(char *fd) {
	int c, res;
	struct termios oldtio,newtio;
	char buf[255];
	int i, rs, sum = 0, speed = 0;

		/*
	if ( (argc < 2) ||
	 	((strcmp("/dev/ttyS0", argv[1])!=0) &&
	 	(strcmp("/dev/ttyS4", argv[1])!=0) )) {
	 	printf("Usage:\tnserial SerialPort\n\tex: nserial /dev/ttyS1\n");
	 	exit(1);
	 }*/


	/*
	Open serial port device for reading and writing and not as controlling tty
	because we don't want to get killed if linenoise sends CTRL-C.
	*/

	close(*fd);
	rs = open(fd, O_RDWR | O_NOCTTY );
	if (rs <0) {perror(fd); exit(-1); }

	if ( tcgetattr(rs,&oldtio) == -1) { /* save current port settings */
		perror("tcgetattr");
		exit(-1);
	}

	bzero(&newtio, sizeof(newtio));
	newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
	newtio.c_iflag = IGNPAR;
	newtio.c_oflag = 0;

	// set input mode (non-canonical, no echo,...)
	newtio.c_lflag = 0;

	newtio.c_cc[VTIME]    = 0;   /* inter-character timer unused */
	newtio.c_cc[VMIN]     = 1;   /* blocking read until 5 chars received*/

	   // VTIME e VMIN devem ser alterados de forma a proteger com um temporizador a
	    //leitura do(s) próximo(s) caracter(es)

	tcflush(rs, TCIOFLUSH);

	if ( tcsetattr(rs,TCSANOW,&newtio) == -1) {
		perror("tcsetattr");
		exit(-1);
	}

	return rs;

}

unsigned int byte_stuffing(unsigned char *data, unsigned char *stuffed_data, int length) {
	if(DEBUG) printf("[stuffing] START\n");
	unsigned int i;
	unsigned int j = 0;
	int lengthAfterStuffing = length;
	
	for(i = 0; i < length; i++) {
		if (data[i] == FLAG || data[i] == ESC) { //Fazemos stuffing em 2 situações: detecção FLAG ou do byte ESC
			stuffed_data[j] = ESC;
			stuffed_data[j+1] = (STFF ^ data[i]); //STFF corresponde a 0x20. Este valor vale 0x5E ou 0x5D conforme a situação
			j++;
			lengthAfterStuffing++;
		}
		else{
			stuffed_data[j] = data[i];
		}
		j++;
	}


	return lengthAfterStuffing;
}

int sendSupervisionFrame(int fd, unsigned char C) {
	if(DEBUG) printf("[sendSup] START\n");
	int n_bytes = 0;
	
	unsigned char BBC1;
	if(DEBUG) printf("[sendSup] LinkLayer Sequence number: %d\n" , linkLayer.sequenceNumber);
	if(linkLayer.sequenceNumber == 1){ //se sequenceNumber for 1 o receptor "pede-nos" uma trama do tipo 1
		if(C == RR){
			C = RR | (1 << 7);
		}else if(C == REJ){
			C = REJ | (1 << 7);
		}
	}

	if(DEBUG) printf("Send supervision 0x%x", C);

	BBC1 = A ^ C;

	if(DEBUG) printf("[sendSup] A = %x, C= %x, BCC1 = %x\n", A, C, BBC1);
	linkLayer.frame[0] = FLAG;
	linkLayer.frame[1] = A;
	linkLayer.frame[2] = C;
	linkLayer.frame[3] = BBC1;
	linkLayer.frame[4] = FLAG;
	
	n_bytes = write(fd, linkLayer.frame, 5);
	if(DEBUG) printf("%x %x %x %x %x\n",linkLayer.frame[0], linkLayer.frame[1], linkLayer.frame[2], linkLayer.frame[3], linkLayer.frame[4]);
	if(DEBUG) printf("[sendSup] Bytes written: %d\n", n_bytes);
	return n_bytes;
}

int sendInformationFrame(unsigned char * data, int length) {
	if(DEBUG) printf("\n[SENDIF] START\n[SENDIF]length = %d", length);
	unsigned char stuffed_data[(MAX_SIZE*2)];
	int C = linkLayer.sequenceNumber << 6;
	int BCC1 = A ^ C;
	int i;
	unsigned char* tmpbuffer = malloc(length +1);
	
	//Se não funcionar, tentar usar um buffer auxiliar para onde vai
	//a data antes de fazer estas operações
	
	//[MOTA] Não funcionou porque tinhas de meter o BCC2 para stuffing também!!
	

	//De forma a calcular BBC2 simplesmente fazemos o calculo do XOR
	//de cada byte de data com o proximo e desse resultado com o proximo.
	//O cálculo de BBC2 é realizado antes do byte stuffing
	int BCC2 = data[0];

	for(i = 1; i < length; i++){
		BCC2 = (BCC2 ^ data[i]);
	}
	
	memcpy(tmpbuffer, data, length);
	tmpbuffer[length] = BCC2;
	
	unsigned int lengthAfterStuffing = byte_stuffing(tmpbuffer, stuffed_data, length+1);

	linkLayer.frame[0] = FLAG;
	linkLayer.frame[1] = A;
	linkLayer.frame[2] = C;
	linkLayer.frame[3] = BCC1;
	memcpy(linkLayer.frame+4, stuffed_data, lengthAfterStuffing);
	linkLayer.frame[4+lengthAfterStuffing] = FLAG;
	
	if(DEBUG) printf("[SENDINF] length After Stuffing = %d\nSending : ", lengthAfterStuffing);
	if(DEBUG) {
		for(i = 0 ; i < lengthAfterStuffing+5; i++)
			printf("0x%x ", linkLayer.frame[i]);
	}
	
	if(DEBUG) printf("[SENDINF] END\n");
	
	linkLayer.sequenceNumber = linkLayer.sequenceNumber ^ 1;
	return (int) write(linkLayer.fd, linkLayer.frame, lengthAfterStuffing+5); // "+6" pois somamos 2FLAG,2BCC,1A,1C
	
}

int receiveframe(unsigned char *data, int* length) {
	if(DEBUG) printf("[receiveframe] START\n");
	
	int state = 0; //state machine state
	int stop = FALSE; //control flag
	int Rreturn; //for return
	int Type; //type of frame received
	int num_chars_read = 0; //number of chars read
	
	unsigned char* charread = malloc(1); //char in serial port
	unsigned char Aread, Cread; //adress and command
	
	//State machine
	while(stop == FALSE) {
		//printf("[receiveframe] State machine -> %d\n", state);
		Rreturn = read(linkLayer.fd, charread, 1); //read 1 char
		if (Rreturn == 1 && DEBUG) printf("[Receiveframe] read -> %x (%d)\n", *charread, Rreturn);
		if (Rreturn < 0) return -1; //nothing

		switch(state) {
			case 0:{ //State Start
				if(DEBUG) printf("[receiveframe] START, char = %x\n", *charread);
				if(*charread == FLAG) state = 1;
				break;
			}

			case 1:{ //Flag -> expect address
				if(*charread == A) {
					if(DEBUG)printf("[receiveframe] ADRESS, char = %x\n", *charread);
					state = 2;
					Aread = *charread;
				} else if(*charread == FLAG) state = 1; //another flag
				break;
			}

			case 2:{ //Address -> Command (many commands possible)
				if(DEBUG) printf("[receiveframe] COMMAND, char = %x ->", *charread);
				Cread = *charread;
				if(*charread == SET)  {
					if(DEBUG) printf("SET\n");
					Type = SET_RECEIVED;
					state = 3;
				}

				else if(*charread == UA) {
					if(DEBUG) printf("UA\n");
					Type = UA_RECEIVED;
					state = 3;
					}

				else if(*charread == DISC) {
					if(DEBUG) printf("DISC\n");
					Type = DISC_RECEIVED;
					state = 3;
				}

				else if(*charread == (RR | (linkLayer.sequenceNumber << 7))) {
					if(DEBUG) printf("RR\n");
					Type = RR_RECEIVED;
					state = 3;
				}

				else if(*charread == (REJ | (linkLayer.sequenceNumber << 7))) {
					if(DEBUG) printf("REJ\n");
					Type = REJ_RECEIVED;
					state = 3;
				}

				else if(*charread == (linkLayer.sequenceNumber << 6)) {
					if(DEBUG) printf("DATA\n");
					Type = DATA_RECEIVED;
					state = 3;
				}

				else if(*charread == FLAG) state = 1;
				else state = 0;
				break;
				}

			case 3:{ //command -> BBC1
				if(DEBUG) printf("[receiveframe] BCC, char = %x\n", *charread);
				if(*charread == FLAG) state = 1;

				else if(*charread != (Aread ^ Cread))
				{
					if(DEBUG) printf("[receiveframe] BCC INCORRECT\n");
					state = 0; //bcc
				}

				else
				{
					if(DEBUG) printf("[receiveframe] BCC CORRECT\n");
					if(Type == DATA_RECEIVED) state = 4;
					else state = 6;
				}
				break;
			}

			case 4:{ //Data expected
				if(DEBUG) printf("[receiveframe] DATA EXPECTED\n");
				
				if (*charread == FLAG)
					{
						unsigned char BCC2exp = data[0];
						int j;
						
						for(j = 1;j < num_chars_read - 1; j++) 
							BCC2exp = (BCC2exp ^ data[j]);
							
						if(data[num_chars_read -1] != BCC2exp) {
							sendSupervisionFrame(linkLayer.fd,REJ);
							return -1;
						}
						else
						{
							
							*length = num_chars_read - 1;
							if(DEBUG) {
								printf("[receiveframe] Lenght: %d Buffer : ", *length);
								puts(data);
							}
							if(DEBUG) printf("[receiveframe] \nSending RR\n");
							linkLayer.sequenceNumber = linkLayer.sequenceNumber ^ 1;
							sendSupervisionFrame(linkLayer.fd, RR);
							free(charread);
							return Type;
							
						}
						
					}

					else if(*charread == ESC) state = 5; //deshuffel o proximo

					else
					{
						if(DEBUG) printf("[RECEIVEFRAME] Char to buffer : %c (number: %d)\n",*charread, num_chars_read); 
						data[num_chars_read] = *charread;
						num_chars_read++;
						state = 4;
					}

					break;
				}

			case 5:{ //Destuffing

				data[num_chars_read] = *charread ^ STFF;
				num_chars_read++;
				state = 4;
				break;
			}

			case 6:{ //Flag
				if(*charread == FLAG)
				{ //flag
					stop = TRUE;
					state = 0;
				}
				else state = 0;
				break;
			}
			if(DEBUG) printf(",%d]", state);
		}
	}
	
	if(DEBUG) printf("[receiveframe] STOP -> %d\n", Type);
	free(charread);
	return Type;

}

/*
void Timeout_SET() {
	if(linkLayer.numTransmissions < MAXR){
		printf("[DEBUG] Timeout %d waiting for UA, re-sending SET\n",  linkLayer.numTransmissions);
		sendSupervisionFrame(linkLayer.fd, SET);
		alarm(linkLayer.timeout);
	}else{
		printf("[DEBUG] Receiver doesnt seem to like me, I give up :(\n");
			//llclose();
		}
	linkLayer.numTransmissions++;

}
void Timeout_RR() {
	
	if(DEBUG) printf("[Timeout_RR] alarme = %d\n", linkLayer.numTransmissions);
	linkLayer.numTransmissions++;

}
*/

int llopen(char* port, int txrx) {
	if(DEBUG) printf("\n[LLOPEN] START\n");

	linkLayer.fd = config(port);
	linkLayer.sequenceNumber = 0;
	linkLayer.State = txrx;

	if(txrx == TRANSMITTER) {
		if(DEBUG) printf("[llopen] TRANSMITTER\n");
		int tmpvar;
		linkLayer.numTransmissions = 0;
		linkLayer.timeout = MAXT;
		
		//(void) signal(SIGALRM, Timeout_SET);
		if(DEBUG) printf("[llopen] Send SET\n");
		sendSupervisionFrame(linkLayer.fd, SET);
		//alarm(linkLayer.timeout);
		if(DEBUG) printf("[llopen] EXPETING UA\n");
		
		tmpvar = receiveframe(NULL,NULL);
		if( tmpvar != UA_RECEIVED ){
			if(DEBUG) printf("[LLOPEN - TRANSMITTER] NOT UA\n");
			return -1; //return error
		}
		else if(tmpvar == UA_RECEIVED) {
			if(DEBUG) printf("[LLOPEN - TRANSMITTER] UA RECEIVED\n");
			return fd; //retorn file ID
		}
		return -1;

	}

	else if (txrx == RECEIVER) {
		if(DEBUG) printf("[llopen] RECEIVER\n");
		if(DEBUG) printf("Waiting for SET\n");
		if(receiveframe(NULL,NULL) != SET_RECEIVED)
		{
			//TODO falta cenas;
			 if(DEBUG) printf("DID NOT RECEIVE SET");
			return -1;
		}
		sendSupervisionFrame(linkLayer.fd,UA);
		if(DEBUG) printf("UA sent\n");

		return fd;
	}

	return 0;
}

int llread(int fd,unsigned char* buffer) {
	if(DEBUG) printf("[LLREAD] START\n");
	int num_chars_read = 0, aux_num_chars = 0;
	unsigned char aux[MAX_SIZE]; 
	//= calloc(MAX_SIZE, sizeof(unsigned char));
	linkLayer.sequenceNumber = 0;
	int Type;
	int i = 0;
	
	do {
		Type= receiveframe(aux, &aux_num_chars);
		//aux[aux_num_chars] = '\0';
			
		if(DEBUG) printf("aux = %s, aux_num_chars = %d\n",aux, aux_num_chars);
		
		if(Type == DATA_RECEIVED)
		{
			memcpy(buffer + (i * MAX_SIZE), aux, aux_num_chars);
			i++;
			num_chars_read += aux_num_chars;
			aux_num_chars = 0;
			//puts(buffer);
		}
		else if(Type == DISC_RECEIVED) {
			llclose(linkLayer.fd);
			return num_chars_read;
		}
	}while(1);
	return -1;
}

int llwrite(int fd, unsigned char* buffer, int length) {
    if(DEBUG) printf("\n[LLWRITE] START\n");

	linkLayer.timeout = 0;
	int CompleteFrames =  length / (MAX_SIZE);
	int remainingBytes =  length % (MAX_SIZE);
	int flag = 1;
	if(DEBUG)printf("[LLWRITE] lenght = %d, complete Frames = %d , remaining bytes = %d\n", length, CompleteFrames, remainingBytes);
	//(void) signal(SIGALRM, Timeout_RR);
	linkLayer.numTransmissions = 0;
	linkLayer.timeout = MAXT;
	linkLayer.sequenceNumber = 0;

	int i;
	for(i = 0; i < CompleteFrames; i++){

		flag = 1;
		while(linkLayer.numTransmissions < MAXT && flag) {
			if(DEBUG) printf("[LLWRITE] Frames = %d\n", i);

			//if(alarm_flag){

				sendInformationFrame(buffer + (i * MAX_SIZE), MAX_SIZE);
				//alarm(linkLayer.timeout);                 				// activa alarme de 3s
				//alarm_flag=0;
			//}
			int tmp = receiveframe(NULL,NULL);
			if(DEBUG) printf("[LLWRITE] tmp : %d\n", tmp);
			if(tmp != RR_RECEIVED) {
				
				if(DEBUG) printf("[LLWRITE] DID NOT RECEIVE RR\n");
				if(tmp == REJ_RECEIVED) {
					
					if(DEBUG) if(DEBUG) printf("[LLWRITE] RECEIVED REJ\n");
					linkLayer.numTransmissions = 0;
					i--;
					
				}
				else return -1;
			}
			else if(tmp == RR_RECEIVED) {
				if(DEBUG) printf("[LLWRITE] RECEIVE RR\n");
				flag =0;
			}

		}
		
		if(DEBUG) printf("[LLWRITE] complete Frames = %d\n", i);
		linkLayer.numTransmissions = 0;
	}

	if(remainingBytes > 0){
		printf("[LLWRITE] BytesRemaining\n");
		flag = 1;
		while(linkLayer.numTransmissions < MAXT && flag) {

			//if(alarm_flag){

				sendInformationFrame(buffer + (i * MAX_SIZE), remainingBytes);
		//		alarm(linkLayer.timeout);                 				// activa alarme de 3s
		//		alarm_flag=0;
		//	}

			int tmp = receiveframe(NULL,NULL);
			if(DEBUG) printf("[LLWRITE] tmp : %d\n", tmp);
			if(tmp != RR_RECEIVED) {
				if(DEBUG) printf("[LLWRITE] DID NOT RECEIVE RR\n");
				if(tmp == REJ_RECEIVED) {
					if(DEBUG) printf("[LLWRITE] RECEIVED REJ\n");
					linkLayer.numTransmissions = 0;
				}
				else return -1;
			}
			else if(tmp == RR_RECEIVED) {
				if(DEBUG) printf("[LLWRITE] RECEIVE RR\n");
				flag =0;
			}
       }

	}
	llclose(linkLayer.fd);
	if(DEBUG) printf("[LLWRITE] END\n");
	return 0;

}

int llclose(int fd) {
	if(DEBUG) printf("[LLCLOSE] START\n");
	int tmp;
	
	sendSupervisionFrame(linkLayer.fd, DISC);
	tmp = receiveframe(NULL,NULL);
	
	if(tmp == DISC_RECEIVED)	{
		if(DEBUG) printf("[LLCLOSE] Expecting UA\n");
		sendSupervisionFrame(linkLayer.fd, UA);
		close(linkLayer.fd);
		return 1;
	}
	
	else if(tmp == UA_RECEIVED)	{
		if(DEBUG) printf("[LLCLOSE] EXITING AS RECEIVER\n");
		close(linkLayer.fd);
		return 1;
	}
	
	else {
		if(DEBUG) printf("[LLCLOSE] PROBLEMS\n");
		return 1;
	}
}


