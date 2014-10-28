all:
	gcc erp_udp.c -pthread -o erp_udp 

clean:
	rm erp_udp