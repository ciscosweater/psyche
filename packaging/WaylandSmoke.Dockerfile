FROM psyche-runtime-check:0.3
RUN apt-get update && apt-get install -y --no-install-recommends weston \
    && rm -rf /var/lib/apt/lists/*
