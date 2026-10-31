FROM node:22-bookworm-slim
# Codex uses the system trust store; Node/npm success alone does not verify it.
RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates \
    && rm -rf /var/lib/apt/lists/*
ARG CODEX_VERSION=0.164.0-alpha.11.1
RUN npm install -g @openai/codex@${CODEX_VERSION} && npm cache clean --force
WORKDIR /app
COPY package.json provider.mjs server.mjs ./
COPY web/ ./web/
RUN mkdir -p /home/node/.codex && chown -R node:node /app /home/node/.codex
ENV CODEX_HOME=/home/node/.codex HOST=0.0.0.0 PORT=8787 SOURCE=demo
USER node
EXPOSE 8787
CMD ["node", "server.mjs"]
