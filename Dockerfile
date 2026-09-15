FROM ubuntu:24.04 AS backend-build

ARG BUILD_JOBS=2
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        libdrogon-dev \
        libjsoncpp-dev \
        libpq-dev \
        libssl-dev \
        uuid-dev \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt main.cc ./
COPY controllers ./controllers
COPY models ./models
COPY test ./test
RUN cmake -S . -B /build -DCMAKE_BUILD_TYPE=Release
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
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        libdrogon1t64 \
        libpq5 \
        python3-minimal \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --home-dir /app --shell /usr/sbin/nologin card-game \
    && install -d -o card-game -g card-game /app/run

WORKDIR /app/run
COPY --from=backend-build /build/card_game /app/card_game
COPY deploy/docker/config.json /app/config.template.json
COPY deploy/docker/backend-entrypoint.py /app/backend-entrypoint.py
USER card-game
EXPOSE 5555
ENTRYPOINT ["python3", "/app/backend-entrypoint.py"]

FROM nginx:1.27-alpine AS frontend
COPY deploy/docker/nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=frontend-build /src/frontend/dist/ /usr/share/nginx/html/
