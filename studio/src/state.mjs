export function isDirty(file, content, original) {
  return Boolean(file) && content !== original;
}
