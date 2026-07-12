FROM python:3.11-slim

WORKDIR /app

# Установка системных зависимостей
RUN apt-get update && apt-get install -y \
    postgresql-client \
    libpq-dev \
    gcc \
    && rm -rf /var/lib/apt/lists/*

# Копирование requirements.txt и установка Python зависимостей
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt gunicorn

# Копирование всего проекта
COPY . .

# Сборка статики (если есть)
RUN python manage.py collectstatic --noinput 2>/dev/null || true

# Переменные окружения по умолчанию
ENV PYTHONDONTWRITEBYTECODE=1
ENV PYTHONUNBUFFERED=1

EXPOSE 8000

# Запуск через gunicorn (не dev-сервер!)
CMD ["gunicorn", "config.wsgi:application", "--bind", "0.0.0.0:8000"]
