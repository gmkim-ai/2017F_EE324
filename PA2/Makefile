all:	clean multi_server select_server client
multi_server:
	gcc -o multi_server multi_server.c
select_server:
	gcc -o select_server select_server.c
client:
	gcc -pthread -o client client.c
clean:
	rm -f multi_server select_server client