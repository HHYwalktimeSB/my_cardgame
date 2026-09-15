FROM ubuntu:24.04 AS backend-build

ARG BUILD_JOBS=2
ARG DROGON_VERSION=v1.9.13
ARG DROGON_REPOSITORY=https://github.com/drogonframework/drogon.git
ARG TRANTOR_REPOSITORY=https://github.com/an-tao/trantor.git
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        git \
        libjsoncpp-dev \
        libpq-dev \
        libssl-dev \
        uuid-dev \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

RUN git clone --branch "$DROGON_VERSION" --depth 1 \
        "$DROGON_REPOSITORY" /drogon \
    && trantor_commit="$(git -C /drogon ls-tree HEAD trantor | awk '{print $3}')" \
    && test -n "$trantor_commit" \
    && git clone "$TRANTOR_REPOSITORY" /drogon/trantor \
    && git -C /drogon/trantor checkout "$trantor_commit" \
    && rm -rf /drogon/.git /drogon/trantor/.git
RUN cmake -S /drogon -B /drogon/build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/drogon \
        -DBUILD_SHARED_LIBS=ON \
        -DBUILD_POSTGRESQL=ON \
        -DBUILD_MYSQL=OFF \
        -DBUILD_SQLITE=OFF \
        -DBUILD_REDIS=OFF \
        -DBUILD_BROTLI=OFF \
        -DBUILD_YAML_CONFIG=OFF \
        -DBUILD_EXAMPLES=OFF \
        -DBUILD_CTL=OFF \
        -DBUILD_TESTING=OFF
RUN cmake --build /drogon/build --parallel "$BUILD_JOBS" --target install

WORKDIR /src
COPY CMakeLists.txt main.cc ./
COPY controllers ./controllers
COPY models ./models
COPY test ./test
RUN cmake -S . -B /build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=/opt/drogon
RUN cmake --build /build --parallel "$BUILD_JOBS" \
    --target card_game card_game_test
RUN /build/test/card_game_test

FROM node:22-alpine AS frontend-build
WORKDIR /src/frontend
COPY frontend/package.json frontend/package-lock.json ./
RUN npm ci
COPY frontend/ ./
RUN npm run build

FROM ubuntu:24.04 AS backend

ENV DEBIAN_FRONTEND=noninteractive
ENV LD_LIBRARY_PATH=/opt/drogon/lib:/opt/drogon/lib64
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        libjsoncpp25 \
        libpq5 \
        libssl3t64 \
        libuuid1 \
        python3-minimal \
        zlib1g \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --home-dir /app --shell /usr/sbin/nologin card-game \
    && install -d -o card-game -g card-game /app/run

WORKDIR /app/run
COPY --from=backend-build /build/card_game /app/card_game
COPY --from=backend-build /opt/drogon/ /opt/drogon/
COPY deploy/docker/config.json /app/config.template.json
COPY deploy/docker/backend-entrypoint.py /app/backend-entrypoint.py
USER card-game
EXPOSE 5555
ENTRYPOINT ["python3", "/app/backend-entrypoint.py"]

FROM nginx:1.27-alpine AS frontend
COPY deploy/docker/nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=frontend-build /src/frontend/dist/ /usr/share/nginx/html/
