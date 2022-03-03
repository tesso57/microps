FROM ubuntu:20.04

RUN apt-get update
RUN apt-get install -y build-essential git iproute2 iputils-ping netcat-openbsd
RUN git clone https://github.com/tesso57/microps.git
