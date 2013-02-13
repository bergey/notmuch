;; notmuch-anything.el --- integrating notmuch with anything / helm
;;
;; Copyright © Daniel Bergey
;;
;; This file is part of Notmuch.
;;
;; Notmuch is free software: you can redistribute it and/or modify it
;; under the terms of the GNU General Public License as published by
;; the Free Software Foundation, either version 3 of the License, or
;; (at your option) any later version.
;;
;; Notmuch is distributed in the hope that it will be useful, but
;; WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;; General Public License for more details.
;;
;; You should have received a copy of the GNU General Public License
;; along with Notmuch.  If not, see <http://www.gnu.org/licenses/>.

(defun anything-notmuch-count-searches ()
  (let ((searches (notmuch-hello-query-counts
                   (if notmuch-saved-search-sort-function
                       (funcall notmuch-saved-search-sort-function
                                notmuch-saved-searches)
                     notmuch-saved-searches)
                   :show-empty-searches notmuch-show-empty-saved-searches)))
    (mapcar (lambda (s) (let ((name (first s))
                            (query (second s))
                            (msg-count (third s)))
                          (cons (format "%8s %s" (notmuch-hello-nice-number msg-count) name) query)))
          searches)))

(defvar anything-notmuch-saved-searches
  '((name . "Notmuch Mail Searches")
    (candidates . anything-notmuch-count-searches)
    (action . (lambda (query) (notmuch-search query notmuch-search-oldest-first)))))

(defun anything-notmuch-saved-searches ()
  (interactive)
  (anything-other-buffer 'anything-notmuch-saved-searches "*Anything Notmuch*"))

(provide 'notmuch-anything)
