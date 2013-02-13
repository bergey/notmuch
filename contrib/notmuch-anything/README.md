# Notmuch Anything

Notmuch-anything defines a hook to list saved searches through emacs's
anything library.  After running M-x anything-notmuch-saved-searches,
type any part of the search name to narrow the available searches.

Notmuch-anything uses the same notmuch-saved-searches variable as
notmuch-hello; there is no separate configuration.  The number of
messages in each search is listed beside the name; saved searches with
no messages are not listed at all.

# Anything / Helm

Further documentation of Anything is available on
[EmacsWiki](http://www.emacswiki.org/Anything) and
[github](https://github.com/emacs-helm/helm).  Recently, Anything has
been renamed Helm, as reflected in the links above.  Since most
distributions (including ELPA) still seem to be using slightly older
versions, notmuch-anything uses the old function names for now.
