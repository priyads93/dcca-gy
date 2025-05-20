FROM ubuntu:20.04
LABEL Description="An Ubuntu-based and simple Docker image of freeDiameter"

ENV DEBIAN_FRONTEND=noninteractive

# Updating the packages
RUN apt-get update
# Installing the freeDiameter dependencies
RUN apt-get -y install mercurial cmake make gcc g++ bison flex libsctp-dev libgnutls28-dev libgcrypt-dev pkg-config libidn11-dev ssl-cert debhelper fakeroot \
libpq-dev libmysqlclient-dev libxml2-dev swig python3-dev
# Downloading the code

WORKDIR /root 
COPY . /root 
RUN mkdir -p /root/fDBuild  

# Making the freeDiameter code
WORKDIR /root/fDBuild 
RUN ls -l ../ && cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
RUN make -j$(nproc)
RUN make install

COPY certs/ /etc/ssl/certs/


WORKDIR /root/extensions/dcca_gy
RUN gcc -Wall -fPIC -shared -o dcca_gy.so dcca_gy.c $(pkg-config --cflags --libs freediameter)

WORKDIR /root
RUN mkdir -p /etc/freeDiameter
COPY freeDiameter.conf /etc/freeDiameter/ 


COPY script.sh /root/
RUN chmod +x /root/script.sh
RUN chmod 755 /root/extensions/dcca_gy/dcca_gy.so
ENTRYPOINT /root/script.sh


EXPOSE 3868 5658
