check-format:
	@for f in lib/*.[ch] tool/*.[ch]; do \
		echo "  CHECK-FORMAT $$f"; \
		astyle --quiet --options=$(abs_srcdir)/.astylerc < $$f | cmp -s $$f -; \
		if [ $$? -ne 0 ]; then \
			astyle --quiet --options=$(abs_srcdir)/.astylerc < $$f | diff -u $$f -; \
			exit 1; \
		fi; \
	done

format:
	@for f in lib/*.[ch] tool/*.[ch]; do \
		echo "  FORMAT $$f"; \
		astyle --quiet --options=$(abs_srcdir)/.astylerc $$f; \
	done
endif
.PHONY: check-format
.PHONY: format

install-tree: all
	rm -rf $(abs_builddir)/install-tree
	$(MAKE) install DESTDIR=$(abs_builddir)/install-tree
	tree $(abs_builddir)/install-tree
.PHONY: install-tree
