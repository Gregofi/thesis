FROM debian:bullseye

RUN apt update -y && apt upgrade -y && \
    apt install -y texlive

# Keep those two RUN separate to create new layer for texlive, which is large,
# every change in package then took forever.
RUN apt install -y texlive-bibtex-extra biber \
                   texlive-xetex \
                   texlive-lang-czechslovak \
                   latexmk \
                   git \
                   make
