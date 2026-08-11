FROM python:3.13-alpine
WORKDIR /app
COPY server.py /app/server.py
RUN adduser -D -u 10001 registry && mkdir -p /data && chown registry:registry /data
USER registry
EXPOSE 8080
HEALTHCHECK --interval=2s --timeout=1s --retries=10 CMD wget -q -O /dev/null http://127.0.0.1:8080/health || exit 1
CMD ["python", "/app/server.py"]
