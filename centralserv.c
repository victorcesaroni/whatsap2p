/* 
* centralserv.c
* REDES A - PROJETO 2: 
* WHATSAP2P
* DIOGO ESTEVES FURTADO 15153927
* LEONARDO RODRIGUES GUISSI 15108244
* VICTOR FERNANDO CESARONI 15593866
*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <wait.h>
#include <sys/shm.h>
#include <pthread.h>
#include <unistd.h>

#include "networklib.h"
#include "linkedlist.h"
#include "shareddefs.h"

struct servidor {
	struct linked_list clientes;
};

static struct servidor g_servidor;

static pthread_mutex_t lista_clientes_lock;

int cliente_comparador(struct linked_list_node* node, void *item) {
	struct user* c1 = (struct user*)node->data;
	struct user* c2 = (struct user*)item;
	//printf("%s %s\n",c1->celular, c2->celular);
	if (strcmp(c1->celular, c2->celular) == 0)
		return 1;
	return 0;
}

/*int teste_comparador(struct linked_list_node* node, int item) {
	if ((int)node->data == item)
		return 1;
	return 0;
}*/

void *client_handler(void *p) {
	struct user *c = (struct user*)p;
	
	while (1) {
		struct logic_packet raw_pkt;
		if (recv_packet(c->socket, &raw_pkt) < 0) {
			printf("[CS] Nao foi possivel ler um pacote\n");
			break;
		}
		
		if (raw_pkt.msg_type == MSG_HAND_SHAKE) {
			struct packet_hand_shake *pkt = (struct packet_hand_shake*)&raw_pkt.data;
			strcpy(c->celular, pkt->celular);
			c->connected = 1;
			c->port_p2p = pkt->port;
			printf("RECV MSG_HAND_SHAKE celular:%s, conexao:%s:%d\n", c->celular, inet_ntoa(c->ip), c->port_p2p);
		}
		else if (raw_pkt.msg_type == MSG_QUERY_INFO) {
			struct packet_query_info *pkt = (struct packet_query_info*)&raw_pkt.data;
			printf("RECV MSG_QUERY_INFO celular:%s\n", pkt->celular);

			pthread_mutex_lock(&lista_clientes_lock);
			struct user tmp;
			strcpy(tmp.celular,pkt->celular);
			struct linked_list_node *node = buscar_lista(&g_servidor.clientes, cliente_comparador, (void*)&tmp); 
			pthread_mutex_unlock(&lista_clientes_lock);

			if (node == NULL) {	
				printf("CLIENTE NAO FOI ENCONTRADO\n");			
				struct packet_query_response qr_pkt;
				qr_pkt.port_p2p = 0;
				qr_pkt.connected = 0;
				strcpy(qr_pkt.celular, pkt->celular);

				BUILD_PACKET(packet_query_response, MSG_QUERY_RESPONSE, raw_pkt_send, qr_pkt);

				if (send_packet(c->socket, &raw_pkt_send) == -1) {
					perror("send()");
					exit(6);
				}
			} else {
				printf("CLIENTE FOI ENCONTRADO\n");
				struct packet_query_response qr_pkt;
				qr_pkt.ip = ((struct user*)node->data)->ip;
				qr_pkt.port_p2p = ((struct user*)node->data)->port_p2p;
				qr_pkt.connected = 1;
				strcpy(qr_pkt.celular, pkt->celular);
				
				BUILD_PACKET(packet_query_response, MSG_QUERY_RESPONSE, raw_pkt_send, qr_pkt);

				if (send_packet(c->socket, &raw_pkt_send) == -1) {
					perror("send()");
					exit(6);
				}
			}
		}
		/*else if (raw_pkt.msg_type == MSG_QUERY_RESPONSE) {
			struct packet_query_response *pkt = (struct packet_query_response*)&raw_pkt.data;
			printf("RECV MSG_QUERY_RESPONSE\n");
		}
		else if (raw_pkt.msg_type == MSG_TEXT) {
			struct packet_text *pkt = (struct packet_text*)&raw_pkt.data;
			printf("RECV MSG_TEXT\n");
		}
		else if (raw_pkt.msg_type == MSG_FILE_PART) {
			struct packet_file_part *pkt = (struct packet_file_part*)&raw_pkt.data;
			printf("RECV MSG_FILE_PART\n");
		}*/
	}
	
	c->connected = 0;
	close(c->socket);
	
	pthread_mutex_lock(&lista_clientes_lock);
	remover_lista(&g_servidor.clientes, &cliente_comparador, (void*)c);
	pthread_mutex_unlock(&lista_clientes_lock);
	memset(c, 0, sizeof(struct user));
	free(c);
}

int main(int argc, char **argv) {
	unsigned short port;                
	struct sockaddr_in client; 
	struct sockaddr_in server; 
	int s;
	int ns;
	
	g_servidor.clientes.head = NULL;
	
	/*inserir_lista(&g_servidor.clientes, (void*)1);
	inserir_lista(&g_servidor.clientes, (void*)2);
	inserir_lista(&g_servidor.clientes, (void*)3);
	inserir_lista(&g_servidor.clientes, (void*)4);
	inserir_lista(&g_servidor.clientes, (void*)5);
	inserir_lista(&g_servidor.clientes, (void*)6);
	
	struct linked_list_node *node = g_servidor.clientes.head;
	while (node) { printf("%d\n", node->data); node = node->next; }
	
	struct linked_list_node *q = buscar_lista(&g_servidor.clientes, teste_comparador, 4);
	printf("%d\n", q->data);
	
	remover_lista(&g_servidor.clientes, teste_comparador, 2);    
	node = g_servidor.clientes.head;
	while (node) { printf("%d\n", node->data); node = node->next; }
	 
	remover_lista(&g_servidor.clientes, teste_comparador, 1);    
	node = g_servidor.clientes.head;
	while (node) { printf("%d\n", node->data); node = node->next; }*/
	
	if (argc != 2) {
		fprintf(stderr, "Use: %s porta\n", argv[0]);
		exit(-1);
	}

	if (pthread_mutex_init(&lista_clientes_lock, NULL) != 0) {
		perror("pthread_mutex_init()");
		exit(1);
	}

	port = (unsigned short)atoi(argv[1]);

	if ((s = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket()");
		exit(2);
	}    
	
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int)) < 0) {
		perror("setsockopt()");
		exit(3);    
	}
	
	if (setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &(int){ 1 }, sizeof(int)) < 0) {
		perror("setsockopt()");
		exit(4);    
	}

	server.sin_family = AF_INET;   
	server.sin_port   = htons(port);       
	server.sin_addr.s_addr = INADDR_ANY;

	if (bind(s, (struct sockaddr *)&server, sizeof(server)) < 0) {
		perror("bind()");
		exit(5);
	}
	
	if (listen(s, 1) != 0) {
		perror("listen()");
		exit(6);
	}

	while (1) {
		pthread_t t;	    
		int namelen = sizeof(client);
		
		if ((ns = accept(s, (struct sockaddr *)&client, &namelen)) == -1) {
			perror("accept()");
			exit(7);
		}
		
		struct user *c = (struct user*)malloc(sizeof(struct user));
		strcpy(c->celular, "test");
		c->ip = client.sin_addr;
		c->port = ntohs(client.sin_port);
		c->socket = ns;
		c->connected = 0;
		pthread_mutex_lock(&lista_clientes_lock);
		inserir_lista(&g_servidor.clientes, (void*)c);
		pthread_mutex_unlock(&lista_clientes_lock);
		
		if (pthread_create(&t, NULL, &client_handler, (void *)c)) {
			perror("pthread_create()");
			exit(8);
		}
	}
	
	pthread_mutex_destroy(&lista_clientes_lock);
	
	return 0;	
}



