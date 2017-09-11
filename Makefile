all: build
	ninja -C build
.PHONY: all

build:
	meson build

clean:
	rm -rf build/
.PHONY: clean

install: build
	ninja -C build install
.PHONY: install

check: build
	meson test -C build --wrap=valgrind
.PHONY: check

format:
	@for f in lib/*.[ch] tool/*.[ch]; do \
		echo $$f; \
		astyle --quiet --options=.astylerc $$f; \
	done
.PHONY: format

install-tree: build
	rm -rf build/install-tree
	DESTDIR=install-tree ninja -C build install
	tree build/install-tree
.PHONY: install-tree
