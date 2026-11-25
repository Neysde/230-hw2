# Same image the GitHub grading workflow uses, so local builds match the grader.
FROM gokceuludogan/cmpe230-spring26:latest

USER root

# Make sure the tools needed for building, grading, and debugging are present.
RUN if command -v apt-get >/dev/null 2>&1; then \
        apt-get update && \
        DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
            build-essential make gdb python3 git ca-certificates && \
        rm -rf /var/lib/apt/lists/*; \
    elif command -v apk >/dev/null 2>&1; then \
        apk add --no-cache build-base make gdb python3 git; \
    fi
