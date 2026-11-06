FROM ubuntu:24.04

WORKDIR /app
ENV PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1 \
    DRAWBORAD_NATIVE_ENGINE=/app/DrawboradHeadless DRAWBORAD_CLOUD_DATA=/data \
    PATH="/opt/drawborad/bin:$PATH"

COPY tools/packaging/install-runtime-ubuntu.sh /tmp/install-runtime.sh
COPY cloud/requirements.txt /tmp/requirements.txt
RUN bash /tmp/install-runtime.sh \
    && apt-get install -y --no-install-recommends python3-venv ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && python3 -m venv /opt/drawborad \
    && /opt/drawborad/bin/pip install --no-cache-dir --only-binary=:all: -r /tmp/requirements.txt opencv-python-headless \
    && groupadd -g 10001 studio && useradd -u 10001 -g studio -M studio \
    && install -d -m 0700 -o studio -g studio /data

COPY DrawboradHeadless /app/DrawboradHeadless
COPY cloud/ /app/cloud/
COPY tools/models/sam3_semantic.py /app/tools/models/sam3_semantic.py
COPY tools/segmentation/guided_grabcut.py /app/tools/segmentation/guided_grabcut.py
USER studio
EXPOSE 8765
HEALTHCHECK --interval=10s --timeout=4s --start-period=10s CMD python -c "import urllib.request; urllib.request.urlopen('http://127.0.0.1:8765/healthz', timeout=3).close()"
CMD ["python", "-m", "cloud.cli", "serve", "--host", "0.0.0.0", "--port", "8765"]
