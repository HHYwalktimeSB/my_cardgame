# 内网单机部署

## Docker Compose 部署（推荐用于服务器）

服务器安装 Docker Engine 和 Compose 插件后，在项目根目录执行：

```bash
cp .env.example .env
# 编辑 .env，将 POSTGRES_PASSWORD 换成随机强密码
docker compose up -d --build
docker compose ps
docker compose logs -f backend
```

默认通过服务器的 80 端口访问。PostgreSQL 数据保存在命名卷
`postgres-data` 中，`db/schema.sql` 仅在首次创建该数据卷时执行。
Docker 构建默认同时编译两个 C++ 文件；内存较小的服务器可在 `.env` 中设置
`BUILD_JOBS=1`。

更新代码后执行：

```bash
git pull
docker compose up -d --build
```

停止服务使用 `docker compose down`。不要在仍需保留数据库时添加 `-v`。

以下内容是无需 Docker 的 systemd 部署方案。

该方案使用 Nginx 提供前端和反向代理，Drogon 只监听本机地址，并由
systemd 管理。开发环境的 `config.json`、Vite proxy 和 `start-dev.sh` 不会被修改。

## 前置依赖

- C++17 编译器、CMake、Drogon 开发库
- Node.js 和 npm
- PostgreSQL 客户端及服务器
- Nginx、systemd、Python 3

## 初始化数据库

先在 PostgreSQL 中创建用户和空数据库，然后执行：

```bash
DATABASE_URL='postgresql://cardgame_user:密码@127.0.0.1/cardgame_db' \
  ./deploy/init-database.sh
```

`db/schema.sql` 只适合空数据库，不要在已有数据库上重复运行。

## 构建和安装

```bash
./deploy/build-release.sh

sudo DB_PASSWORD='数据库密码' \
  SERVER_NAME='服务器内网IP或域名' \
  CARD_GAME_THREADS=2 \
  ./deploy/install-server.sh
```

默认安装位置：

- 程序：`/opt/card-game`
- 配置：`/etc/card-game/config.json`
- 服务：`card-game.service`
- Nginx：`/etc/nginx/conf.d/card-game.conf`
- 日志：`/var/log/card-game`

后续更新代码时重新运行构建和安装脚本即可。已有服务器配置默认不会被覆盖；
需要重新生成配置时显式设置 `OVERWRITE_CONFIG=1`。

## 检查

```bash
systemctl status card-game
journalctl -u card-game -f
nginx -t
curl http://服务器内网IP/
curl http://127.0.0.1:5555/metrics
```

如果服务器还运行其他 Nginx 站点，请先调整 `nginx.conf.template` 中的
`default_server`，并移除安装脚本中删除默认站点的操作。

本地开发仍使用：

```bash
./start-dev.sh
```
