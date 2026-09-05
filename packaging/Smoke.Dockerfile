FROM debian:trixie-slim
ENV LANG=C.UTF-8
RUN apt-get update && apt-get install -y --no-install-recommends \
    libegl1 libopengl0 libglx0 ca-certificates fonts-dejavu-core \
    && rm -rf /var/lib/apt/lists/*
# No Qt, QML, libarchive, or yaml-cpp packages are installed here.
