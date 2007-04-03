
(defun kill-oel-spaces ()
  (interactive)
  (goto-char 1)
  (replace-regexp "[ \t]*$" "")
  )

(defun untabify-buffer ()
  (interactive)
  (end-of-buffer)
  (untabify 1 (point) )
  )

(defun indent-buffer ()
  (interactive)
  (end-of-buffer)
  (indent-region 1 (point) nil)
  )

(defun fix-brace-on-if-statements ()
  (interactive)
  (goto-char 1)
  (replace-regexp "^\\([ \t]*if.*\\)[{]" "\\1
{")
  )

(defun fix-brace-on-else-statements ()
  (interactive)
  (goto-char 1)
  (replace-regexp "^[ \t]*[}][ \t]+else+[ \t][{]" "}
else
{")
  )

;; the fixing flow...
(setq target (getenv "CFILE"))
(find-file target)
(fix-brace-on-if-statements)
(fix-brace-on-else-statements)
(indent-buffer)
(kill-oel-spaces)
(untabify-buffer)
(save-buffer)
(kill-emacs)

