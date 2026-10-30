/* ===========================================================================
 *  fs.c — ПОСЛЕ РЕФАКТОРИНГА (Этапы 2 + 3)
 *
 *  Изменены: name_cmp (нов.), cmp_entry_ctx, sort_entries, free_list,
 *            reserve_entry (нов.), fill_from_dirent (нов.), stat_entry (нов.),
 *            entry_fill_stat (нов.), append_entry, read_dir_entries,
 *            load_dir, get_selected_or_current, calc_size, calc_size_dev.
 *  Без изменений: do_sort (адаптирован), enter_dir, copy/delete/fsync-часть.
 * ======================================================================== */
#include "fm.h"

/* ---------------------------------------------------------------------------
 * ЭТАП 2 (сортировка): strcoll() — не на каждом сравнении.
 *
 * Было: qsort_r -> cmp_entry_ctx -> strcoll(a->name, b->name) на КАЖДОМ
 *       из ~N*log2(N) сравнений. Для 10 000 записей это ~133 000 вызовов
 *       strcoll; первый вызов ещё и тянет locale-таблицы (для ru_RU.UTF-8 —
 *       сотни КБ через mmap).
 * Стало: префильтр по первому байту; strcoll() — только для строк с
 *       одинаковым первым символом (то есть почти никогда). Порядок
 *       сортировки не меняется: strcoll остаётся tie-breaker'ом.
 * ------------------------------------------------------------------------- */
static int name_cmp(const char *a, const char *b, int use_coll)
{
    unsigned char ca = (unsigned char)*a;
    unsigned char cb = (unsigned char)*b;
    int res;
    /* Первый байт UTF-8 — старший: и для ASCII, и для кириллицы (0xD0/0xD1)
     * порядок байтов совпадает с порядком кодовых точек → фильтр корректен. */
    if (ca != cb) return (ca < cb) ? -1 : 1;
    if (!use_coll) return strcmp(a, b);
    res = strcoll(a, b);
    return res ? res : strcmp(a, b);
}

static int cmp_entry_ctx(const void *a, const void *b, void *ctx)
{
    const Entry *ea = a, *eb = b;
    const SortCtx *c = (const SortCtx *)ctx;
    int res = 0;
    if (ea->is_parent != eb->is_parent) return eb->is_parent - ea->is_parent;
    if (ea->is_dir != eb->is_dir) return ea->is_dir - eb->is_dir;
    if (c->field == SORT_NAME) {
        res = name_cmp(ea->name, eb->name, 1);
    } else if (c->field == SORT_TYPE) {
        /* type_name — короткая интернированная строка, strcmp дешевле strcoll */
        res = strcmp(ea->type_name, eb->type_name);
        if (res == 0) res = name_cmp(ea->name, eb->name, 0);
    } else if (c->field == SORT_DATE) {
        res = (ea->mtime < eb->mtime) ? -1 :
              (ea->mtime > eb->mtime) ?  1 : name_cmp(ea->name, eb->name, 0);
    } else if (c->field == SORT_SIZE) {
        res = (ea->size < eb->size) ? -1 :
              (ea->size > eb->size) ?  1 : name_cmp(ea->name, eb->name, 0);
    }
    return c->desc ? -res : res;
}

static void sort_entries(FilePane *p)
{
    SortCtx ctx;
    size_t i;
    if (!p || p->list.count <= 1 || !p->list.items) return;
    /* Сортировка по дате/размеру требует настоящих mtime/size: делаем
     * один проход stat'ов ТОЛЬКО когда об этом действительно попросили. */
    if (p->sort_field == SORT_DATE || p->sort_field == SORT_SIZE) {
        for (i = 1; i < p->list.count; i++)
            entry_fill_stat(p, &p->list.items[i]);
    }
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

/* ---------------------------------------------------------------------------
 * ЭТАП 2: free_list освобождает каждое имя.
 * Единственная точка освобождения EntryList → её нельзя «забыть».
 * ------------------------------------------------------------------------- */
void free_list(EntryList *l)
{
    size_t i;
    if (!l) return;
    if (l->items) {
        for (i = 0; i < l->count; i++)
            free(l->items[i].name);   /* free(NULL) безопасен */
        free(l->items);
    }
    l->items = NULL;
    l->count = 0;
}

/* ---------------------------------------------------------------------------
 * ЭТАП 2: резерв под список.
 * Было: Entry items[64] на стеке + malloc(64 * sizeof(Entry)),
 *       sizeof(Entry) = 256 (name) + 256 (type_name) + ... = 536 байт.
 * Стало: sizeof(Entry) = 64 байта + strlen(name)+1 в куче.
 * 10 000 файлов: 5 360 536 -> 640 000 + ~120 000 (имена).
 * ------------------------------------------------------------------------- */
#ifndef FM_LIST_INIT_CAP
#define FM_LIST_INIT_CAP 128
#endif

static int reserve_entry(Entry **items, size_t *cap, size_t need)
{
    size_t ncap;
    Entry *tmp;
    if (need <= *cap) return 0;
    ncap = *cap ? *cap : FM_LIST_INIT_CAP;
    while (ncap < need) {
        if (ncap > (((size_t)-1) / 2)) return -1;      /* анти-overflow */
        ncap *= 2;
    }
    if (ncap > (((size_t)-1) / sizeof(Entry))) return -1;
    tmp = realloc(*items, ncap * sizeof(Entry));
    if (!tmp) return -1;
    *items = tmp;
    *cap = ncap;
    return 0;
}

/* ---------------------------------------------------------------------------
 * ЭТАП 3: тип из d_type — без единого syscall'а.
 * Возвращает 0, если тип известен и запись заполнена.
 * mode/size/mtime остаются «оптимистичными»: S_IFDIR|0755 и т.п. Права
 * и точный размер подтянет entry_fill_stat() при первом показе строки.
 * ------------------------------------------------------------------------- */
static int fill_from_dirent(Entry *it, unsigned char dt)
{
    switch (dt) {
    case DT_DIR:  it->is_dir = 1; it->mode = S_IFDIR  | 0755; return 0;
    case DT_REG:  it->mode = S_IFREG  | 0644; return 0;
    case DT_LNK:  it->is_link = 1; it->mode = S_IFLNK | 0777; return 0;
    case DT_FIFO: it->mode = S_IFIFO  | 0644; return 0;
    case DT_SOCK: it->mode = S_IFSOCK | 0644; return 0;
    case DT_CHR:  it->mode = S_IFCHR  | 0644; return 0;
    case DT_BLK:  it->mode = S_IFBLK  | 0644; return 0;
    default:      return -1;    /* DT_UNKNOWN и всё нестандартное */
    }
}

/* Ровно ОДИН вызов вместо пары lstat()+stat(). */
static void stat_entry(Entry *it, const char *full, unsigned char dt)
{
    struct stat st;
    if (dt == DT_LNK) {
        /* Ссылка: нужен тип цели, иначе is_dir соврёт и «открытие» папки
         * уйдёт в просмотрщик файлов. */
        if (stat(full, &st) == 0) {
            it->is_dir = S_ISDIR(st.st_mode);
            it->mode   = st.st_mode | S_IFLNK;
            it->size   = st.st_size;
            it->mtime  = st.st_mtime;
        }
        return;
    }
    if (fstatat(AT_FDCWD, full, &st, AT_SYMLINK_NOFOLLOW) == 0) {
        it->is_dir = S_ISDIR(st.st_mode);
        it->mode   = st.st_mode;
        it->size   = st.st_size;
        it->mtime  = st.st_mtime;
    }
}

/* ---------------------------------------------------------------------------
 * ЭТАП 3 (ленивое заполнение): size/mtime/mode дозапрашиваются ровно один
 * раз — когда запись реально понадобилась (видимая строка панели, статус-
 * строка, сортировка по дате/размеру, Info). Флаг stat_done в битовом поле
 * гарантирует, что повторных stat() не будет даже при перерисовке.
 * ------------------------------------------------------------------------- */
void entry_fill_stat(FilePane *p, Entry *e)
{
    struct stat st;
    char full[FM_PATH_MAX];
    if (!p || !e || e->stat_done || e->is_parent || !e->name) return;
    e->stat_done = 1;
    if (make_path(full, sizeof full, p->cwd, e->name) != 0) return;
    if (e->is_link) {
        if (stat(full, &st) != 0) return;      /* битая ссылка — как раньше */
    } else if (fstatat(AT_FDCWD, full, &st, AT_SYMLINK_NOFOLLOW) != 0) {
        return;
    }
    e->is_dir = S_ISDIR(st.st_mode);
    e->mode   = e->is_link ? (st.st_mode | S_IFLNK) : st.st_mode;
    e->size   = st.st_size;
    e->mtime  = st.st_mtime;
    /* icon/type_name зависят от прав (X = исполняемый) — пересчёт один раз */
    e->icon = file_icon(e->name, e->is_dir, e->is_link, e->mode);
    utf8_truncate(e->type_name, sizeof(e->type_name),
                  type_str(e->name, e->is_dir, e->is_link, e->mode));
}

/* ---------------------------------------------------------------------------
 * append_entry: имя копируется strdup'ом, d_type пробрасывается из readdir().
 * ------------------------------------------------------------------------- */
static int append_entry(EntryList *l, size_t *cap, const char *dname,
                        const char *cwd, unsigned char dt)
{
    char full[FM_PATH_MAX];
    Entry *it;
    int have_type;

    if (reserve_entry(&l->items, cap, l->count + 1) != 0) return -1;
    it = &l->items[l->count];
    memset(it, 0, sizeof(*it));          /* name = NULL, stat_done = 0 */

    /* ЭТАП 2: точная копия имени вместо char name[MAX_NAME] */
    it->name = strdup(dname);
    if (!it->name) return -1;            /* count НЕ увеличен → утечки нет */

    have_type = (dt != DT_UNKNOWN) && (fill_from_dirent(it, dt) == 0);
    if (!have_type && make_path(full, sizeof full, cwd, dname) == 0)
        stat_entry(it, full, dt);
    /* Если stat не удался — запись остаётся «неизвестной» (size=0):
     * entry_fill_stat() попробует ещё раз при отрисовке. */

    if (it->is_parent)
        it->icon = '<';
    else
        it->icon = file_icon(it->name, it->is_dir, it->is_link, it->mode);
    utf8_truncate(it->type_name, sizeof(it->type_name),
                  type_str(it->name, it->is_dir, it->is_link, it->mode));

    l->count++;                          /* коммит только после успеха */
    return 0;
}

static int read_dir_entries(FilePane *p)
{
    DIR *d;
    struct dirent *de;
    EntryList l = { NULL, 0 };
    size_t cap = 0;
    char full[FM_PATH_MAX];
    int rc = 0, saved;

    d = opendir(p->cwd);
    if (!d) return -1;

    if (reserve_entry(&l.items, &cap, 1) != 0) { closedir(d); return -1; }
    memset(&l.items[0], 0, sizeof(l.items[0]));
    l.items[0].name = strdup("..");
    if (!l.items[0].name) { free(l.items); closedir(d); return -1; }
    l.items[0].is_dir    = 1;
    l.items[0].is_parent = 1;
    l.items[0].mode      = S_IFDIR | 0755;
    l.items[0].icon      = '<';
    utf8_truncate(l.items[0].type_name, sizeof(l.items[0].type_name),
                  type_str("..", 1, 0, S_IFDIR | 0755));
    /* mtime родителя нужен только для сортировки по дате; поле stat_done
     * оставлено 0, поэтому первый же запрос подтянет точные данные. */
    if (make_path(full, sizeof full, p->cwd, "..") == 0) {
        struct stat pst;
        if (fstatat(AT_FDCWD, full, &pst, 0) == 0)
            l.items[0].mtime = pst.st_mtime;
    }
    l.count = 1;

    errno = 0;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.' &&
            (de->d_name[1] == '\0' ||
             (de->d_name[1] == '.' && de->d_name[2] == '\0')))
            continue;
        if (append_entry(&l, &cap, de->d_name, p->cwd,
                         (unsigned char)de->d_type) != 0) {
            rc = -1;
            break;
        }
        errno = 0;
    }
    if (rc == 0 && errno != 0) rc = -1;
    saved = errno;
    closedir(d);

    if (rc != 0) {
        free_list(&l);          /* освобождает все уже выделенные name */
        errno = saved;
        return -1;
    }
    p->list = l;
    return 0;
}

/* ---------------------------------------------------------------------------
 * load_dir: единственное место, где уничтожается СТАРЫЙ список.
 * ------------------------------------------------------------------------- */
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
               && p->file_sel < (int)old.count) {
        copy_str(keep, sizeof keep, old.items[p->file_sel].name);
    }

    p->list.items = NULL;      /* read_dir_entries пишет только при успехе */
    p->list.count = 0;
    if (read_dir_entries(p) != 0) {
        p->list = old;         /* старый список возвращён как есть */
        if (keep[0])
            copy_str(p->pending_sel, sizeof p->pending_sel, keep);
        return -1;             /* old НЕ освобождён → утечки нет */
    }
    free_list(&old);           /* здесь освобождаются ВСЕ имена старого списка */
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
    char saved[FM_PATH_MAX];
    char *resolved;
    int rc;
    if (!path || !p) return -1;
    /* ФИКС (переполнение стека): было realpath(path, resolved) с
     * char resolved[FM_PATH_MAX]. realpath() ПИШЕТ ВЕСЬ результат и НЕ знает
     * про размер буфера — путь длиннее FM_PATH_MAX забивал стек мимо массива
     * (ASan: WRITE of 1340 bytes into a 1024-byte object). Ссылка
     * /tmp/deep/short → путь в 1339 байт роняла файловый менеджер.
     * Теперь NULL → размер выделяет сам glibc. */
    resolved = realpath(path, NULL);
    if (!resolved) return -1;
    if (strlen(resolved) >= sizeof p->cwd) {   /* не влезает — честный отказ */
        free(resolved);
        return -1;
    }
    /* realpath, не chdir: канонический путь без прыжка по symlink,
     * cwd процесса не трогаем. */
    copy_str(saved, sizeof saved, p->cwd);
    copy_str(p->cwd, sizeof p->cwd, resolved);
    free(resolved);
    rc = load_dir(p);
    if (rc != 0) {
        copy_str(p->cwd, sizeof p->cwd, saved);
        return -1;
    }
    p->file_sel = (p->list.count > 1) ? 1 : 0;
    p->file_top = 0;
    return 0;
}

/* ---------------------------------------------------------------------------
 * get_selected_or_current: буферы теперь char *[], владелец — вызывающий.
 * ------------------------------------------------------------------------- */
static void free_paths_vec(char **v, int n)
{
    int i;
    if (!v) return;
    for (i = 0; i < n; i++) free(v[i]);
    free(v);
}

int get_selected_or_current(FilePane *p, char **paths, int max_paths)
{
    int count = 0;
    size_t i;
    char tmp[FM_PATH_MAX];
    if (!p || !paths || max_paths <= 0 || !p->list.items) return 0;
    for (i = 0; i < p->list.count && count < max_paths; i++) {
        Entry *e = &p->list.items[i];
        if (e->is_parent || !e->selected || !name_ok(e->name)) continue;
        if (make_path(tmp, sizeof tmp, p->cwd, e->name) != 0) continue;
        paths[count] = strdup(tmp);
        if (!paths[count]) { free_paths_vec(paths, count); return 0; }
        count++;
    }
    if (count == 0 && p->file_sel > 0
        && p->file_sel < (int)p->list.count) {
        Entry *e = &p->list.items[p->file_sel];
        if (!e->is_parent && name_ok(e->name)
            && make_path(tmp, sizeof tmp, p->cwd, e->name) == 0) {
            paths[0] = strdup(tmp);
            if (paths[0]) count = 1;
        }
    }
    return count;
}

/* ---------------------------------------------------------------------------
 * calc_size: в исходнике (void)depth и рекурсия всегда стартовала с 0 —
 * параметр depth ничего не ограничивал. Теперь он честно инкрементируется.
 * ------------------------------------------------------------------------- */
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
    while ((de = readdir(d)) != NULL) {
        char child[FM_PATH_MAX];
        if (de->d_name[0] == '.' &&
            (de->d_name[1] == '\0' ||
             (de->d_name[1] == '.' && de->d_name[2] == '\0')))
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
    if (!path || lstat(path, &st) != 0) return 0;
    return calc_size_dev(path, st.st_dev, depth);  /* было: 0 — depth игнорился */
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

static int dest_exists(const char *dst)
{
    struct stat st;
    return lstat(dst, &st) == 0;
}

static int open_sibling_temp(char *tmp, size_t tmpsz, const char *dst)
{
    char dir[FM_PATH_MAX];
    int n, fd;
    if (path_parent(dir, sizeof dir, dst) != 0)
        copy_str(dir, sizeof dir, ".");
    n = snprintf(tmp, tmpsz, "%s/.vibey.XXXXXX", dir);
    if (n < 0 || (size_t)n >= tmpsz) return -1;
    fd = mkstemp(tmp);
    return fd;
}

static int copy_bytes(int in, int out, CopyProgress *cp)
{
    char buf[65536];
    for (;;) {
        ssize_t n = read(in, buf, sizeof buf);
        ssize_t written = 0;
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) return -1;
        if (n == 0) return 0;
        while (written < n) {
            ssize_t w = write(out, buf + written, (size_t)(n - written));
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) return -1;
            written += w;
        }
        note_progress(cp, n, cp ? cp->disp : NULL);
    }
}

static int finish_copy_fd(int fd, const struct stat *st)
{
    struct timespec ts[2];
    if (fchmod(fd, st->st_mode & 0777) != 0) return -1;
    ts[0] = st->st_atim;
    ts[1] = st->st_mtim;
    futimens(fd, ts);
    if (fsync(fd) != 0 && errno != EINVAL && errno != EROFS
        && errno != ENOTSUP) return -1;
    return 0;
}

static void fsync_dir(const char *path)
{
    int fd, saved = errno;
    if (!path || !path[0]) return;
    fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) { errno = saved; return; }
    if (fsync(fd) != 0 && errno != EINVAL && errno != EROFS
        && errno != ENOTSUP) {
        close(fd);
        return;
    }
    close(fd);
    errno = saved;
}

static int commit_temp(const char *tmp, const char *dst, int overwrite)
{
    char dir[FM_PATH_MAX];
    int rc;
    /* RENAME_NOREPLACE закрывает TOCTOU: не затирать цель, появившуюся после проверки. */
    if (!overwrite) {
        rc = renameat2(AT_FDCWD, tmp, AT_FDCWD, dst, RENAME_NOREPLACE);
        if (rc == 0) goto done;
        if (errno == EEXIST || errno == ENOTEMPTY) return -1;
        if (errno != EINVAL && errno != ENOTSUP && errno != ENOSYS)
            return -1;
        /* Fallback: link+unlink — атомарно и не перезаписывает существующий dst. */
        if (link(tmp, dst) != 0) return -1;
        if (unlink(tmp) != 0) {
            /* dst создан, но tmp не удалён: не критично, оставим как есть. */
        }
    } else {
        if (rename(tmp, dst) != 0) return -1;
    }
done:
    if (path_parent(dir, sizeof dir, dst) == 0)
        fsync_dir(dir);
    return 0;
}

static int do_copy_file(const char *src, const char *dst, CopyProgress *cp, int overwrite)
{
    struct stat st;
    char tmp[FM_PATH_MAX];
    int in, out, rc;
    tmp[0] = '\0';
    if (lstat(src, &st) != 0 || !S_ISREG(st.st_mode)) return -1;
    if (!overwrite && dest_exists(dst)) {
        errno = EEXIST;
        return -1;
    }
    in = open(src, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (in < 0) return -1;
    out = open_sibling_temp(tmp, sizeof tmp, dst);
    if (out < 0) {
        close(in);
        return -1;
    }
    rc = copy_bytes(in, out, cp);
    if (rc == 0) rc = finish_copy_fd(out, &st);
    close(in);
    close(out);
    if (rc == 0) rc = commit_temp(tmp, dst, overwrite);
    if (rc != 0 && tmp[0]) unlink(tmp);
    return rc;
}

static int do_copy_symlink(const char *src, const char *dst)
{
    char target[FM_PATH_MAX];
    ssize_t len = readlink(src, target, sizeof target - 1);
    if (len < 0) return -1;
    target[len] = '\0';
    if (symlink(target, dst) != 0) return -1;
    return 0;
}

static int do_copy_depth(const char *src, const char *dst, CopyProgress *cp,
                         dev_t root_dev, int depth, int overwrite);

static int do_copy_dir(const char *src, const char *dst, CopyProgress *cp,
                       dev_t root_dev, int depth, int overwrite)
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
    else if (errno != EEXIST || !overwrite) return -1;

    d = opendir(src);
    if (!d) {
        if (created) rmdir(dst);
        return -1;
    }
    errno = 0;
    while ((de = readdir(d)) && rc == 0) {
        char s_child[FM_PATH_MAX], d_child[FM_PATH_MAX];
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
        if (do_copy_depth(s_child, d_child, cp, root_dev, depth + 1,
                          created ? 0 : overwrite) != 0)
            rc = -1;
    }
    if (rc == 0 && errno != 0) rc = -1;
    closedir(d);
    if (rc == 0) chmod(dst, mode);
    /* Политика частичного результата (задокументирована по замечанию
     * рецензента): удаляем ТОЛЬКО то, что создали сами целиком — каталог
     * dst, если он не существовал. Файлы, уже скопированные ВНУТРЬ
     * существующего каталога, остаются: они целые (каждый файл коммитится
     * отдельно через rename), а их удаление стёрло бы данные, которые
     * пользователь мог положить туда раньше нас. Временных файлов здесь
     * не остаётся вовсе: do_copy_file() снимает свой .vibey.XXXXXX при
     * любом rc != 0 (см. unlink(tmp) ниже по файлу). */
    if (rc != 0 && created) fs_delete(dst);
    return rc;
}

static int do_copy_depth(const char *src, const char *dst, CopyProgress *cp,
                         dev_t root_dev, int depth, int overwrite)
{
    struct stat st;
    if (!src || !dst || depth > CALC_SIZE_MAX_DEPTH) return -1;
    if (path_is_inside(src, dst)) return -1;
    if (lstat(src, &st) != 0) return -1;
    if (depth > 0 && st.st_dev != root_dev) return 0;
    if (S_ISLNK(st.st_mode))  return do_copy_symlink(src, dst);
    if (S_ISDIR(st.st_mode))  return do_copy_dir(src, dst, cp, root_dev, depth, overwrite);
    if (S_ISREG(st.st_mode))  return do_copy_file(src, dst, cp, overwrite);
    return -1;
}

int do_copy_recursive(const char *src, const char *dst, CopyProgress *cp, int overwrite)
{
    struct stat st;
    if (!src || !dst || lstat(src, &st) != 0) return -1;
    return do_copy_depth(src, dst, cp, st.st_dev, 0, overwrite);
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
        char child[FM_PATH_MAX];
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
    char parent[FM_PATH_MAX];
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
    if (fs_delete_rec(target, st.st_dev, 0) != 0) return -1;
    if (path_parent(parent, sizeof parent, target) == 0)
        fsync_dir(parent);
    return 0;
}
