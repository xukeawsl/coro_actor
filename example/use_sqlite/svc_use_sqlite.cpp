#include "coro_actor_ctx.h"
#include "sqlite3.h"

asio::awaitable<void> svc_use_sqlite_impl(coro_actor_pack_t* rqst, coro_actor_pack_t* resp);

extern "C" void svc_use_sqlite(coro_actor_pack_t* rqst, coro_actor_pack_t* resp) {
    asio::co_spawn(g_actor_ctx->io_contex(), svc_use_sqlite_impl(rqst, resp), asio::detached);
}

asio::awaitable<void> svc_use_sqlite_impl(coro_actor_pack_t* rqst, coro_actor_pack_t* resp) {
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int i, rc;
    const char *sql;
    char *zErrMsg;
    int busy_cnt = 0;
    int succ_cnt = 0;
    int total_cnt = 1000000;
    int loop_cnt = total_cnt;

    rc = sqlite3_open_v2("test.db", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if (rc) {
        SPDLOG_ERROR("Can't open database: {}", std::string(sqlite3_errmsg(db)));
        g_actor_ctx->notify_complete();
        co_return;
    }

    /* 当文件处于锁定状态时, 其它操作会立即返回 SQLITE_BUSY, 可以设置一个等待时间 */
    /* 如果在这个时间内拿到文件锁了就正常处理, 否则还是返回 SQLITE_BUSY */
    sqlite3_busy_timeout(db, 50000);

    sql = "CREATE TABLE IF NOT EXISTS COMPANY("
          "ID INT PRIMARY KEY     NOT NULL,"
          "NAME           TEXT    NOT NULL,"
          "AGE            INT     NOT NULL,"
          "ADDRESS        CHAR(50),"
          "SALARY         REAL )";

    rc = sqlite3_exec(db, sql, 0, 0, &zErrMsg);

    if (rc != SQLITE_OK) {
        SPDLOG_ERROR("rc = {}, errMsg = {}", rc, std::string(zErrMsg));
        sqlite3_free(zErrMsg);
        goto over;
    }

    sql = "INSERT INTO COMPANY (ID, NAME, AGE, ADDRESS, SALARY) "
          "VALUES (?, ?, ?, ?, ?)";

    SPDLOG_INFO("start prepare");

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        SPDLOG_ERROR("Can't prepare sql: {}", std::string(sqlite3_errmsg(db)));
        goto over;
    }

    SPDLOG_INFO("start insert");

    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);

    for (i = 0; i < loop_cnt; i++) {
        sqlite3_bind_int64(stmt, 1, time(nullptr) * loop_cnt + i);
        sqlite3_bind_text(stmt, 2, "Tom", -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, 18);

        rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) {
            succ_cnt++;
        } else if (rc == SQLITE_BUSY) {
            busy_cnt++;
        } else if (rc == SQLITE_CONSTRAINT) {
            total_cnt--;
        } else {
            SPDLOG_ERROR("rc = {}, {}", rc, std::string(sqlite3_errmsg(db)));
        }

        sqlite3_reset(stmt);
    }

    sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);

    sqlite3_finalize(stmt);

    SPDLOG_INFO("total_cnt = {}, succ_cnt = {}, busy_cnt = {}", total_cnt, succ_cnt, busy_cnt);

over:
    sqlite3_close_v2(db);
    g_actor_ctx->notify_complete();
    co_return;
}