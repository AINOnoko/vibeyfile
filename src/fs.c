#include "fm.h"

static int cmp_entry_ctx(const void *a, const void *b, void *ctx)
{
    const Entry *ea = a, *eb = b;
    const SortCtx *c = (const SortCtx *)ctx;
    int res = 0;
    if (ea->is_parent != eb->is_parent) return eb->is_parent - ea->is_parent;
    if (ea->is_dir != eb->is_dir) return eb->is_dir - ea->is_dir;
    if (c->field == SORT_NAME) {
        res = strcoll(ea->name, eb->name);
        if (res == 0) res = strcmp(ea->name, eb->name);
    } else if (c->field == SORT_TYPE) {
        const char *ta = type_str(ea->name, ea->is_dir, ea->mode);
        const char *tb = type_str(eb->name, eb->is_dir, eb->mode);
        res = strcoll(ta, tb);
        if (res == 0) res = strcmp(ea->name, eb->name);
    } else if (c->field == SORT_DATE) {
        res = (ea->mtime < eb->mtime) ? -1 :
              (ea->mtime > eb->mtime) ?  1 : strcmp(ea->name, eb->name);
    } else if (c->field == SORT_SIZE) {
        res = (ea->size < eb->size) ? -1 :
              (ea->size > eb->size) ?  1 : strcmp(ea->name, eb->name);
    }
    return c->desc ? -res : res;
}

static void sort_entries(FilePane *p)
{
    SortCtx ctx;
    if (!p || p->list.count <= 1 || !p->list.items) return;
    ctx.field = p->sort_field;
    ctx.desc = p->sort_desc;
    qsort_r(p->list.items + 1, p->list.count - 1, sizeof(*p->list.items),
            cmp_entry_ctx, &ctx);
}

void do_sort(FilePane *p)
{
    char keep_name[MAX_NAME];
    size_t i;
    keep_name[0] = '\0';
    if (!p) return;
    if (p->list.count > 1 && p->file_sel > 0
        && p->file_sel < (int)p->list.count && p->list.items)
        copy_str(keep_name, sizeof keep_name,
                 p->list.items[p->file_sel].name);
    sort_entries(p);
    if (keep_name[0] && p->list.items)
        for (i = 1; i < p->list.count; i++)
            if (strcmp(p->list.items[i].name, keep_name) == 0) {
                p->file_sel = (int)i;
                break;
            }
}

void free_list(EntryList *l)
{
    if (!l) return;
    free(l->items);
    l->items = NULL;
    l->count = 0;
}

static int append_entry(EntryList *l, size_t *cap, const char *dname,
                        const char *cwd)
{
    char full[PATH_MAX];
    struct stat st;
    Entry *it;
    size_t dnl;
    if (l->count == *cap) {
        size_t ncap = *cap ? *cap * 2 : 64;
        Entry *tmp;
        if (ncap < *cap) return -1;
        tmp = realloc(l->items, ncap * sizeof(*tmp));
        if (!tmp) return -1;
        l->items = tmp;
        *cap = ncap;
    }
    it = &l->items[l->count];
    memset(it, 0, sizeof(*it));
    dnl = strlen(dname);
    if (dnl >= sizeof(it->name)) dnl = sizeof(it->name) - 1;
    memcpy(it->name, dname, dnl);
    it->name[dnl] = '\0';
    if (make_path(full, sizeof full, cwd, dname) == 0) {
        if (stat(full, &st) == 0 || lstat(full, &st) == 0) {
            it->is_dir = S_ISDIR(st.st_mode);
            it->mode   = st.st_mode;
            it->size   = st.st_size;
            it->mtime  = st.st_mtime;
        }
    }
    l->count++;
    return 0;
}

static int read_dir_entries(FilePane *p)
{
    DIR *d = opendir(p->cwd);
    struct dirent *de;
    EntryList l = { NULL, 0 };
    size_t cap = 64;
    int rc = 0;
    if (!d) return -1;
    l.items = malloc(cap * sizeof(*l.items));
    if (!l.items) { closedir(d); return -1; }
    memset(&l.items[0], 0, sizeof(l.items[0]));
    snprintf(l.items[0].name, MAX_NAME, "..");
    l.items[0].is_dir    = 1;
    l.items[0].is_parent = 1;
    l.items[0].mode      = S_IFDIR | 0755;
    l.count = 1;
    errno = 0;
    while ((de = readdir(d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        if (append_entry(&l, &cap, de->d_name, p->cwd) != 0) {
            rc = -1;
            break;
        }
        errno = 0;
    }
    if (rc == 0 && errno != 0) rc = -1;
    closedir(d);
    if (rc != 0) { free(l.items); return -1; }
    p->list = l;
    return 0;
}

int load_dir(FilePane *p)
{
    EntryList old;
    char keep[MAX_NAME];
    size_t i;
    if (!p) return -1;
    keep[0] = '\0';
    old = p->list;
    if (p->pending_sel[0]) {
        copy_str(keep, sizeof keep, p->pending_sel);
        p->pending_sel[0] = '\0';
    } else if (old.items && p->file_sel > 0
               && p->file_sel < (int)old.count)
        copy_str(keep, sizeof keep, old.items[p->file_sel].name);

    p->list.items = NULL;
    p->list.count = 0;
    if (read_dir_entries(p) != 0) {
        p->list = old;
        if (keep[0])
            copy_str(p->pending_sel, sizeof p->pending_sel, keep);
        return -1;
    }
    free(old.items);
    sort_entries(p);
    if (keep[0] && p->list.items) {
        for (i = 0; i < p->list.count; i++)
            if (strcmp(p->list.items[i].name, keep) == 0) {
                p->file_sel = (int)i;
                return 0;
            }
    }
    if (p->file_sel >= (int)p->list.count)
        p->file_sel = p->list.count ? (int)p->list.count - 1 : 0;
    if (p->file_sel < 0) p->file_sel = 0;
    return 0;
}

int enter_dir(const char *path, FilePane *p)
{
    char resolved[PATH_MAX];
    char saved[PATH_MAX];
    if (!path || !p) return -1;
    if (!realpath(path, resolved)) return -1;
    copy_str(saved, sizeof saved, p->cwd);
    copy_str(p->cwd, sizeof p->cwd, resolved);
    if (load_dir(p) != 0) {
        copy_str(p->cwd, sizeof p->cwd, saved);
        return -1;
    }
    p->file_sel = (p->list.count > 1) ? 1 : 0;
    p->file_top = 0;
    return 0;
}

static off_t calc_size_dev(const char *path, dev_t root_dev, int depth)
{
    struct stat st;
    DIR *d;
    struct dirent *de;
    off_t total = 0;
    if (depth > CALC_SIZE_MAX_DEPTH) return 0;
    if (lstat(path, &st) != 0) return 0;
    if (S_ISLNK(st.st_mode)) return 0;
    if (depth > 0 && st.st_dev != root_dev) return 0;
    if (!S_ISDIR(st.st_mode)) return st.st_size;
    d = opendir(path);
    if (!d) return 0;
    while ((de = readdir(d))) {
        char child[PATH_MAX];
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        if (make_path(child, sizeof child, path, de->d_name) != 0)
            continue;
        total += calc_size_dev(child, root_dev, depth + 1);
    }
    closedir(d);
    return total;
}

off_t calc_size(const char *path, int depth)
{
    struct stat st;
    (void)depth;
    if (!path || lstat(path, &st) != 0) return 0;
    return calc_size_dev(path, st.st_dev, 0);
}

static void note_progress(CopyProgress *cp, off_t n, const char *disp)
{
    int pct;
    if (!cp || n <= 0) return;
    cp->done += n;
    if (!cp->win || cp->total <= 0) return;
    pct = (int)((cp->done * 100) / cp->total);
    if (pct > 100) pct = 100;
    if (pct == cp->last_pct) return;
    show_progress(pct, disp ? disp : "", cp->win);
    cp->last_pct = pct;
}

static int do_copy_file(const char *src, const char *dst, CopyProgress *cp)
{
    struct stat st;
    int in, out, rc = 0;
    char buf[65536];
    if (lstat(src, &st) != 0 || !S_ISREG(st.st_mode)) return -1;
    in = open(src, O_RDONLY | O_NOFOLLOW);
    if (in < 0) return -1;
    out = open(dst, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW,
               st.st_mode & 0777);
    if (out < 0) { close(in); return -1; }
    for (;;) {
        ssize_t n = read(in, buf, sizeof buf);
        ssize_t written = 0;
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { rc = -1; break; }
        if (n == 0) break;
        while (written < n) {
            ssize_t w = write(out, buf + written, (size_t)(n - written));
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) { rc = -1; break; }
            written += w;
        }
        if (rc < 0) break;
        note_progress(cp, n, cp ? cp->disp : NULL);
    }
    if (rc == 0) {
        struct timespec ts[2];
        fchmod(out, st.st_mode & 0777);
        ts[0] = st.st_atim;
        ts[1] = st.st_mtim;
        futimens(out, ts);
    }
    close(in);
    close(out);
    if (rc < 0) unlink(dst);
    return rc;
}

static int do_copy_symlink(const char *src, const char *dst)
{
    char target[PATH_MAX];
    ssize_t len = readlink(src, target, sizeof target - 1);
    if (len < 0) return -1;
    target[len] = '\0';
    if (symlink(target, dst) != 0) return -1;
    return 0;
}

static int do_copy_depth(const char *src, const char *dst, CopyProgress *cp,
                         dev_t root_dev, int depth);

static int do_copy_dir(const char *src, const char *dst, CopyProgress *cp,
                       dev_t root_dev, int depth)
{
    DIR *d;
    struct dirent *de;
    struct stat st;
    int rc = 0, created = 0;
    mode_t mode = 0755;
    if (lstat(src, &st) != 0) return -1;
    if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)) return -1;
    if (path_is_inside(src, dst)) return -1;
    mode = st.st_mode & 0777;
    if (mode == 0) mode = 0755;
    if (mkdir(dst, mode) == 0) created = 1;
    else if (errno != EEXIST) return -1;
    d = opendir(src);
    if (!d) {
        if (created) rmdir(dst);
        return -1;
    }
    errno = 0;
    while ((de = readdir(d)) && rc == 0) {
        char s_child[PATH_MAX], d_child[PATH_MAX];
        struct stat cst;
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        if (make_path(s_child, sizeof s_child, src, de->d_name) != 0
            || make_path(d_child, sizeof d_child, dst, de->d_name) != 0) {
            rc = -1;
            break;
        }
        if (lstat(s_child, &cst) == 0 && cst.st_dev != root_dev)
            continue;
        if (do_copy_depth(s_child, d_child, cp, root_dev, depth + 1) != 0)
            rc = -1;
    }
    if (rc == 0 && errno != 0) rc = -1;
    closedir(d);
    if (rc == 0) chmod(dst, mode);
    if (rc != 0 && created) fs_delete(dst);
    return rc;
}

static int do_copy_depth(const char *src, const char *dst, CopyProgress *cp,
                         dev_t root_dev, int depth)
{
    struct stat st;
    if (!src || !dst || depth > CALC_SIZE_MAX_DEPTH) return -1;
    if (path_is_inside(src, dst)) return -1;
    if (lstat(src, &st) != 0) return -1;
    if (depth > 0 && st.st_dev != root_dev) return 0;
    if (S_ISLNK(st.st_mode))  return do_copy_symlink(src, dst);
    if (S_ISDIR(st.st_mode))  return do_copy_dir(src, dst, cp, root_dev, depth);
    if (S_ISREG(st.st_mode))  return do_copy_file(src, dst, cp);
    return -1;
}

int do_copy_recursive(const char *src, const char *dst, CopyProgress *cp)
{
    struct stat st;
    if (!src || !dst || lstat(src, &st) != 0) return -1;
    return do_copy_depth(src, dst, cp, st.st_dev, 0);
}

int get_selected_or_current(FilePane *p, char paths[][PATH_MAX],
                            int max_paths)
{
    int count = 0;
    size_t i;
    if (!p || !paths || max_paths <= 0 || !p->list.items) return 0;
    for (i = 0; i < p->list.count && count < max_paths; i++) {
        Entry *e = &p->list.items[i];
        if (e->is_parent || !e->selected || !name_ok(e->name)) continue;
        if (make_path(paths[count], PATH_MAX, p->cwd, e->name) == 0)
            count++;
    }
    if (count == 0 && p->file_sel > 0
        && p->file_sel < (int)p->list.count) {
        Entry *e = &p->list.items[p->file_sel];
        if (!e->is_parent && name_ok(e->name)
            && make_path(paths[0], PATH_MAX, p->cwd, e->name) == 0)
            count = 1;
    }
    return count;
}

static int fs_delete_rec(const char *path, dev_t root_dev, int depth)
{
    struct stat st;
    DIR *d;
    struct dirent *de;
    int rc = 0;
    if (!path || !*path || depth > CALC_SIZE_MAX_DEPTH) return -1;
    if (lstat(path, &st) != 0) return -1;
    if (S_ISLNK(st.st_mode) || !S_ISDIR(st.st_mode))
        return unlink(path);
    if (st.st_dev != root_dev) {
        errno = EXDEV;
        return -1;
    }
    d = opendir(path);
    if (!d) return -1;
    while ((de = readdir(d))) {
        char child[PATH_MAX];
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        if (make_path(child, sizeof child, path, de->d_name) != 0
            || fs_delete_rec(child, root_dev, depth + 1) != 0)
            rc = -1;
    }
    closedir(d);
    if (rc != 0) return -1;
    return rmdir(path);
}

int fs_delete(const char *target)
{
    struct stat st, pst;
    char parent[PATH_MAX];
    const char *base;
    if (!target || !*target || !strcmp(target, "/")) {
        errno = EPERM;
        return -1;
    }
    base = strrchr(target, '/');
    base = base ? base + 1 : target;
    if (!base[0] || !strcmp(base, ".") || !strcmp(base, "..")) {
        errno = EINVAL;
        return -1;
    }
    if (lstat(target, &st) != 0) return -1;
    if (path_parent(parent, sizeof parent, target) == 0
        && lstat(parent, &pst) == 0 && pst.st_dev != st.st_dev) {
        errno = EXDEV;
        return -1;
    }
    return fs_delete_rec(target, st.st_dev, 0);
}
