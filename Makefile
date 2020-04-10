secrets:
	ncypher decrypt < include/secrets.h.enc > include/secrets.h
all:
	docker-compose up -d 
