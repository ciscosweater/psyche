FROM debian:trixie-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake python3 binutils ca-certificates \
    qt6-base-dev qt6-declarative-dev qt6-wayland qt6-svg-plugins \
    qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts \
    qml6-module-qtquick-dialogs qml6-module-qtquick-window qml6-module-qtquick-templates \
    qml6-module-qtqml-workerscript libarchive-dev libyaml-cpp-dev \
    && rm -rf /var/lib/apt/lists/*
ENV LANG=C.UTF-8
WORKDIR /source
COPY CMakeLists.txt README.md LICENSE CHANGELOG.md ./
COPY src ./src
COPY qml ./qml
COPY tests ./tests
COPY packaging ./packaging
RUN VERSION=$(sed -n 's/^project(Psyche VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt) \
    && test -n "$VERSION" \
    && cmake -S . -B /build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /build -j2 \
    && ctest --test-dir /build --output-on-failure \
    && cmake --install /build --strip --prefix /bundle/psyche-${VERSION}-linux-x86_64 \
    && python3 packaging/bundle.py /bundle/psyche-${VERSION}-linux-x86_64
