FROM balatro-ps3dev:2
RUN apt-get update && apt-get install -y --no-install-recommends openjdk-21-jdk-headless gcc g++ cmake unzip python3-pil && rm -rf /var/lib/apt/lists/*
RUN curl -fL --retry 3 https://services.gradle.org/distributions/gradle-8.14.3-bin.zip -o /tmp/gradle.zip && unzip -q /tmp/gradle.zip -d /opt && rm /tmp/gradle.zip
ENV PATH=/opt/gradle-8.14.3/bin:$PATH
WORKDIR /work
