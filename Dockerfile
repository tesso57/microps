FROM ubuntu:20.04

RUN apt-get update
RUN apt-get install -y build-essential git iproute2 iputils-ping netcat-openbsd

COPY ./ /home
WORKDIR /home
RUN make