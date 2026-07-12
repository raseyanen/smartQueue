# SmartQueue - Система управления очередями

[![Docker Pulls](https://img.shields.io/docker/pulls/moodroow/smartqueue)](https://hub.docker.com/r/moodroow/smartqueue)
[![GitHub release](https://img.shields.io/github/v/release/moodroow/smartqueue)](https://github.com/moodroow/smartqueue/releases)

SmartQueue - это полноценная система управления очередями с поддержкой веб-интерфейса, терминалов на ESP32, матричных принтеров и мобильного доступа.

---

## 📦 Быстрый старт

### Docker Hub
```bash
docker pull moodroow/smartqueue:latest
docker run -p 8000:8000 moodroow/smartqueue:latest
```

### Homebrew (macOS / Linux)
```bash
brew tap raseyanen/smartqueue
brew install smartqueue
smartqueue start
```

После запуска откройте в браузере: [http://localhost:8000](http://localhost:8000)

---

## 🚀 Возможности

### Для пользователей:
- Создание очередей с уникальными короткими ссылками
- Защита очередей паролем
- Занятие мест в очереди и получение талонов
- Живое обновление статуса очереди
- Подсчёт приблизительного времени ожидания
- Уведомления когда подходит очередь
- Обмен местами с другими участниками (до 5 попыток)
- Просмотр истории посещений

### Для владельцев очередей:
- Аннулирование любых талонов
- Бан пользователей (в конкретной очереди или во всех очередях)
- Просмотр списков активных и завершивших очередь участников
- Экспорт данных в CSV
- Управление терминалами

### Для терминалов (ESP32):
- Выдача анонимных талонов с меткой "Т"
- Поддержка матричных принтеров FunnyPrint/Epson
- OLED дисплей 128x64 (опционально)
- WiFi (WPA2-Personal и WPA2-Enterprise)
- Режим точки доступа для настройки
- Веб-интерфейс конфигурации с паролем
- OTA обновления
- Автоматическая генерация хеша на основе MAC адреса

---

## 🏗️ Структура проекта

```
smartQueue/
├── server/                 # Django сервер
│   ├── accounts/          # Приложение аутентификации
│   ├── queues/            # Приложение очередей
│   ├── config/            # Настройки Django
│   ├── manage.py
│   ├── requirements.txt
│   └── Dockerfile
├── terminal/              # Прошивка для ESP32
│   ├── src/
│   │   └── main.cpp
│   ├── include/
│   ├── data/
│   └── platformio.ini
├── bin/                   # Скрипты для Homebrew
│   └── smartqueue
├── docker-compose.yml     # Для разработки
├── docker-compose.prod.yml # Для продакшена
├── .env.example           # Пример переменных окружения
└── README.md
```

---

## 🐳 Запуск через Docker

### Требования
- Docker и Docker Compose
- Git (для клонирования)

### Разработка (с монтированием кода)
```bash
git clone https://github.com/moodroow/smartqueue
cd smartQueue

# Создайте .env файл с переменными
cp .env.example .env

# Запустите
docker-compose up -d

# Сервер доступен по адресу http://localhost:8000
```

### Продакшен (с готовым образом)
```bash
docker-compose -f docker-compose.prod.yml up -d
```

---

## 💻 Локальная разработка

```bash
cd server

# Создание виртуального окружения
python -m venv .venv
source .venv/bin/activate  # Linux/Mac
# или .venv\Scripts\activate  # Windows

# Установка зависимостей
pip install -r requirements.txt

# Миграции базы данных
python manage.py migrate

# Создание суперпользователя
python manage.py createsuperuser

# Запуск сервера
python manage.py runserver
```

---

## 🔧 Переменные окружения

Создайте файл `.env` в корне проекта со следующими переменными:

```env
# Django
SECRET_KEY=your-secret-key-here
DEBUG=False
ALLOWED_HOSTS=localhost,127.0.0.1,yourdomain.com

# База данных
DB_NAME=smartqueue
DB_USER=smartqueue
DB_PASSWORD=secure_password_here
DB_PORT=5438

# Docker Hub (опционально)
DOCKER_USERNAME=moodroow
TAG=latest

# Веб-приложение
WEB_PORT=8000
```

---

## 📡 API Документация

### Терминальное API

#### Получение талона
```http
POST /queues/api/terminal/ticket/
Content-Type: application/json
X-Terminal-Hash: <hash_терминала>

{
    "link": "<ссылка_очереди>",
    "ip_address": "192.168.1.100"
}
```

Ответ:
```json
{
    "success": true,
    "ticket_number": 42,
    "ticket_type": "Т",
    "ticket_hash": "abc123...",
    "message": "Талон успешно создан"
}
```

#### Подтверждение печати
```http
POST /queues/api/terminal/confirm/
Content-Type: application/json
X-Terminal-Hash: <hash_терминала>

{
    "ticket_number": 42,
    "confirmed": true
}
```

#### Проверка талона по хешу
```http
GET /queues/api/ticket/verify/<ticket_hash>/
```

---

## 📊 Модели данных

### Queue (Очередь)
- `owner` - владелец очереди
- `name` - название
- `description` - описание
- `link` - короткая ссылка для доступа
- `password` - пароль на очередь (опционально)
- `is_active` - активна ли очередь

### Ticket (Талон)
- `queue` - очередь
- `user` - пользователь (может быть null для анонимных)
- `number` - порядковый номер
- `ticket_type` - тип ('У' - сайт, 'Т' - терминал)
- `is_cancelled` - отменён ли
- `ticket_hash` - хеш для проверки
- `ip_address` - IP адрес получения

### Terminal (Терминал)
- `owner` - владелец
- `hash_secret` - секретный хеш для API
- `mac_address` - MAC адрес устройства
- `firmware_version` - версия прошивки

### QueueExchange (Обмен местами)
- `from_ticket` - кто предлагает
- `to_ticket` - кому предлагают
- `message` - сообщение
- `status` - статус (pending/accepted/declined/expired)
- `offer_count` - счётчик попыток

### Ban (Бан)
- `queue` - очередь
- `user` - забаненный пользователь
- `scope` - область (queue/owner)
- `reason` - причина

---

## 🖥️ Настройка терминала ESP32

### Подключение пинов
```
ESP32-C3 SuperMicro:
- BUTTON_PIN: GPIO4 (кнопка печати)
- LED_PIN: GPIO5 (индикация)
- PRINTER_RX: GPIO6 (приём от принтера)
- PRINTER_TX: GPIO7 (передача принтеру)
- OLED_SDA: GPIO8 (опционально)
- OLED_SCL: GPIO9 (опционально)
```

### Первоначальная настройка
1. При первом включении терминал создаёт точку доступа "SmartQueue_Terminal"
2. Пароль по умолчанию: "12345678"
3. Подключитесь к сети и откройте http://192.168.4.1
4. Введите логин: admin, пароль: admin
5. Настройте:
   - URL сервера (например, http://192.168.1.100:8000)
   - Хеш терминала (получить в личном кабинете на сервере)
   - WiFi сеть и пароль
   - Пароль веб-интерфейса

### Компиляция прошивки
```bash
cd terminal

# Установка PlatformIO
pip install platformio

# Сборка
pio run

# Загрузка на устройство
pio run --target upload

# Мониторинг последовательного порта
pio device monitor
```

---

## 🛡️ Безопасность

- **Защита от мультиаккаунтов**: Логирование IP адресов, возможность бана по IP
- **Аутентификация терминалов**: Уникальный хеш для каждого терминала
- **Рекомендации**: Используйте HTTPS в продакшене, меняйте SECRET_KEY

---

## 🧪 Тестирование

```bash
cd server

# Запуск тестов
pytest

# Запуск с покрытием
pytest --cov=accounts --cov=queues
```

---

## 🚀 Продакшен

### Рекомендации:
1. Используйте PostgreSQL вместо SQLite
2. Настройте HTTPS (например, через Nginx)
3. Измените SECRET_KEY и пароли
4. Отключите DEBUG режим
5. Используйте gunicorn (уже настроено в Docker)
6. Настройте резервное копирование БД

### Ручной запуск с gunicorn:
```bash
gunicorn config.wsgi:application --bind 0.0.0.0:8000 --workers 4
```

---

## 🤝 Внесение вклада

1. Форкните репозиторий
2. Создайте ветку для вашей фичи (`git checkout -b feature/amazing-feature`)
3. Зафиксируйте изменения (`git commit -m 'Add amazing feature'`)
4. Запушьте ветку (`git push origin feature/amazing-feature`)
5. Откройте Pull Request

---

## 📄 Лицензия

MIT License. См. файл [LICENSE](LICENSE) для подробностей.

---

## 📬 Контакты

Для вопросов и предложений создавайте [Issues](https://github.com/moodroow/smartqueue/issues) в репозитории.
