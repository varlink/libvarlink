all: build
	ninja -C build

check: build
	meson test -C build --wrap=valgrind

build:
	meson build

format:
	@for f in lib/*.[ch] tool/*.[ch]; do \
		echo $$f; \
		astyle --quiet --options=.astylerc $$f; \
	done
.PHONY: format

install-tree: all
	rm -rf build/install-tree
	DESTDIR=install-tree ninja -C build install
	tree build/install-tree
.PHONY: install-tree
