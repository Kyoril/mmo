FROM ubuntu:18.04

# Setup CMake from kitware's repository (see details at https://apt.kitware.com/)
RUN apt-get update && apt-get install -y \
	apt-transport-https \
	ca-certificates \ 
	gnupg \ 
	software-properties-common \
	wget
RUN wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | \
	gpg --dearmor - | \
	tee /etc/apt/trusted.gpg.d/kitware.gpg >/dev/null
RUN apt-add-repository 'deb https://apt.kitware.com/ubuntu/ bionic main'
RUN apt-get update && apt-get install -y \
	kitware-archive-keyring
RUN rm /etc/apt/trusted.gpg.d/kitware.gpg

# Install dependencies necessary to build project
RUN apt-get update && apt-get install -y \
	build-essential \
	cmake \
	autoconf \
	libtool \
	pkg-config \
	zlib1g-dev \
	libssl-dev \
	libmysqlclient-dev

# Build artifacts
COPY . /app

RUN cd /app && \
	mkdir -p build && \
	cd build && \
	cmake /app && \
	make
