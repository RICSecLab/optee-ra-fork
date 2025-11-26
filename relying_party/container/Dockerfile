FROM golang:1.23 AS go-builder

# Install ear/arc tool
RUN go install github.com/veraison/ear/arc@latest

FROM ubuntu:22.04
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get -y install --no-install-suggests --no-install-recommends \
        build-essential \
        ca-certificates \
        curl \
        git \
        libssl-dev \
        make \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

# Install Go 1.23
RUN curl -fsSL https://go.dev/dl/go1.23.3.linux-amd64.tar.gz | tar -C /usr/local -xzf -
ENV PATH="/usr/local/go/bin:$PATH"

# Create an user
ARG USERNAME=user
ARG USER_UID=1000
ARG USER_GID=${USER_UID}
RUN groupadd -o --gid ${USER_GID} ${USERNAME} \
    && useradd --uid ${USER_UID} --gid ${USER_GID} -m ${USERNAME}

# Create app directory
ENV APP_DIR /app
RUN mkdir ${APP_DIR} \
    && chown ${USERNAME}:${USERNAME} ${APP_DIR}

# Copy arc tool from builder
COPY --from=go-builder /go/bin/arc /usr/local/bin/arc
ENV PATH="/home/${USERNAME}/go/bin:/usr/local/bin:$PATH"

# Install user's tools
RUN apt-get update \
    && apt-get -y install --no-install-suggests --no-install-recommends \
        clang-format \
        nano \
        net-tools \
    && rm -rf /var/lib/apt/lists/*

USER ${USERNAME}:${USERNAME}
WORKDIR ${APP_DIR}
ENTRYPOINT [ "bash", "-c", "make && ./rp" ]
