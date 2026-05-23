from django.db import models
from django.contrib.auth.models import User
import uuid
import hashlib
from datetime import timedelta
from django.utils import timezone


class Queue(models.Model):
    """Очередь, создаваемая пользователем"""
    owner = models.ForeignKey(User, on_delete=models.CASCADE, related_name='owned_queues')
    name = models.CharField(max_length=255)
    description = models.TextField(blank=True)
    link = models.CharField(max_length=50, unique=True, editable=False)
    created_at = models.DateTimeField(auto_now_add=True)
    is_active = models.BooleanField(default=True)
    password = models.CharField(max_length=128, blank=True, null=True)  # Пароль на очередь
    
    def save(self, *args, **kwargs):
        if not self.link:
            self.link = str(uuid.uuid4())[:8]
        super().save(*args, **kwargs)

    def __str__(self):
        return f"Queue: {self.name} (by {self.owner.username})"

    class Meta:
        ordering = ['-created_at']


class Ticket(models.Model):
    """Талон участника очереди"""
    TICKET_TYPE_CHOICES = [
        ('U', 'У'),  # Талон с сайта
        ('T', 'Т'),  # Талон с терминала
    ]
    
    queue = models.ForeignKey(Queue, on_delete=models.CASCADE, related_name='tickets')
    user = models.ForeignKey(User, on_delete=models.SET_NULL, null=True, blank=True, related_name='tickets')
    number = models.PositiveIntegerField()
    ticket_type = models.CharField(max_length=1, choices=TICKET_TYPE_CHOICES, default='U')
    created_at = models.DateTimeField(auto_now_add=True)
    is_cancelled = models.BooleanField(default=False)
    cancelled_by_owner = models.BooleanField(default=False)
    is_anonymous = models.BooleanField(default=False)
    printed = models.BooleanField(default=False)
    print_confirmed = models.BooleanField(default=False)
    ip_address = models.GenericIPAddressField(null=True, blank=True)
    
    # Для уведомлений
    notified = models.BooleanField(default=False)
    notified_at = models.DateTimeField(null=True, blank=True)
    
    # Хеш талона для проверки
    ticket_hash = models.CharField(max_length=64, editable=False, blank=True)
    
    def save(self, *args, **kwargs):
        if not self.ticket_hash:
            # Генерация хеша на основе всех данных талона
            timestamp = self.created_at.timestamp() if self.created_at else timezone.now().timestamp()
            hash_data = f"{self.queue.id}:{self.number}:{self.ticket_type}:{timestamp}"
            self.ticket_hash = hashlib.sha256(hash_data.encode()).hexdigest()
        super().save(*args, **kwargs)
    
    def verify_hash(self):
        """Проверка целостности хеша талона"""
        timestamp = self.created_at.timestamp() if self.created_at else timezone.now().timestamp()
        hash_data = f"{self.queue.id}:{self.number}:{self.ticket_type}:{timestamp}"
        expected_hash = hashlib.sha256(hash_data.encode()).hexdigest()
        return self.ticket_hash == expected_hash

    def __str__(self):
        status = "Cancelled" if self.is_cancelled else "Active"
        ticket_prefix = self.get_ticket_type_display()
        return f"Ticket {ticket_prefix}#{self.number} in {self.queue.name} - {status}"

    class Meta:
        ordering = ['number']
        unique_together = ['queue', 'number']


class Terminal(models.Model):
    """Терминал для выдачи талонов"""
    owner = models.ForeignKey(User, on_delete=models.CASCADE, related_name='terminals')
    hash_secret = models.CharField(max_length=64, unique=True, editable=False)
    name = models.CharField(max_length=255)
    created_at = models.DateTimeField(auto_now_add=True)
    is_active = models.BooleanField(default=True)
    
    # Данные для ESP32
    mac_address = models.CharField(max_length=17, blank=True, null=True)
    last_seen = models.DateTimeField(null=True, blank=True)
    firmware_version = models.CharField(max_length=20, blank=True, default="1.0.0")

    def save(self, *args, **kwargs):
        if not self.hash_secret:
            # Если есть MAC адрес, используем его для генерации хеша
            if self.mac_address:
                self.hash_secret = hashlib.sha256(
                    f"{self.mac_address}{timezone.now()}".encode()
                ).hexdigest()
            else:
                self.hash_secret = hashlib.sha256(
                    f"{uuid.uuid4()}{timezone.now()}".encode()
                ).hexdigest()
        super().save(*args, **kwargs)

    def __str__(self):
        return f"Terminal: {self.name} (owner: {self.owner.username})"

    def can_serve_queue(self, queue):
        """Проверка, может ли терминал обслуживать очередь"""
        return queue.owner == self.owner


class QueueExchange(models.Model):
    """Предложение обмена местами в очереди"""
    STATUS_CHOICES = [
        ('pending', 'Ожидает'),
        ('accepted', 'Принят'),
        ('declined', 'Отклонен'),
        ('expired', 'Истек'),
    ]
    
    from_ticket = models.ForeignKey(Ticket, on_delete=models.CASCADE, related_name='exchange_offers_from')
    to_ticket = models.ForeignKey(Ticket, on_delete=models.CASCADE, related_name='exchange_offers_to')
    message = models.TextField(blank=True)
    status = models.CharField(max_length=20, choices=STATUS_CHOICES, default='pending')
    created_at = models.DateTimeField(auto_now_add=True)
    expires_at = models.DateTimeField()
    
    # Счетчик попыток для получателя
    offer_count = models.PositiveIntegerField(default=1)
    max_offers = 5  # Максимум 5 предложений

    def save(self, *args, **kwargs):
        if not self.expires_at:
            self.expires_at = timezone.now() + timedelta(hours=24)
        super().save(*args, **kwargs)

    def __str__(self):
        return f"Exchange: {self.from_ticket} -> {self.to_ticket} ({self.status})"

    class Meta:
        ordering = ['-created_at']


class Notification(models.Model):
    """Уведомления пользователей"""
    TYPE_CHOICES = [
        ('turn', 'Ваша очередь подошла'),
        ('exchange', 'Предложение обмена'),
        ('exchange_accepted', 'Обмен принят'),
        ('exchange_declined', 'Обмен отклонен'),
        ('banned', 'Вы забанены'),
        ('cancelled', 'Талон аннулирован'),
    ]
    
    user = models.ForeignKey(User, on_delete=models.CASCADE, related_name='notifications')
    notification_type = models.CharField(max_length=20, choices=TYPE_CHOICES)
    message = models.TextField()
    ticket = models.ForeignKey(Ticket, on_delete=models.SET_NULL, null=True, blank=True, related_name='notifications')
    exchange = models.ForeignKey(QueueExchange, on_delete=models.SET_NULL, null=True, blank=True, related_name='notifications')
    is_read = models.BooleanField(default=False)
    created_at = models.DateTimeField(auto_now_add=True)

    def __str__(self):
        return f"Notification for {self.user.username}: {self.notification_type}"

    class Meta:
        ordering = ['-created_at']


class Ban(models.Model):
    """Бан пользователей в очередях"""
    SCOPE_CHOICES = [
        ('queue', 'В одной очереди'),
        ('owner', 'Во всех очередях владельца'),
    ]
    
    queue = models.ForeignKey(Queue, on_delete=models.CASCADE, related_name='bans')
    user = models.ForeignKey(User, on_delete=models.CASCADE, related_name='bans')
    scope = models.CharField(max_length=10, choices=SCOPE_CHOICES, default='queue')
    reason = models.TextField(blank=True)
    created_at = models.DateTimeField(auto_now_add=True)
    created_by = models.ForeignKey(User, on_delete=models.SET_NULL, null=True, related_name='bans_created')

    def __str__(self):
        return f"Ban: {self.user.username} in {self.queue.name} ({self.scope})"

    class Meta:
        unique_together = ['queue', 'user', 'scope']


class IPAddressLog(models.Model):
    """Логирование IP адресов для предотвращения мультиаккаунтов"""
    user = models.ForeignKey(User, on_delete=models.CASCADE, related_name='ip_logs')
    ip_address = models.GenericIPAddressField()
    created_at = models.DateTimeField(auto_now_add=True)

    def __str__(self):
        return f"{self.user.username} - {self.ip_address}"

    class Meta:
        ordering = ['-created_at']
        indexes = [
            models.Index(fields=['ip_address']),
        ]


class CompletedTicket(models.Model):
    """Архив завершивших очередь талонов"""
    TICKET_TYPE_CHOICES = [
        ('U', 'У'),
        ('T', 'Т'),
    ]
    
    queue = models.ForeignKey(Queue, on_delete=models.SET_NULL, null=True, related_name='completed_tickets')
    user = models.ForeignKey(User, on_delete=models.SET_NULL, null=True, blank=True)
    number = models.PositiveIntegerField()
    ticket_type = models.CharField(max_length=1, choices=TICKET_TYPE_CHOICES)
    created_at = models.DateTimeField()
    completed_at = models.DateTimeField(auto_now_add=True)
    ip_address = models.GenericIPAddressField(null=True, blank=True)
    ticket_hash = models.CharField(max_length=64)
    
    def __str__(self):
        return f"Completed Ticket #{self.number} in {self.queue.name}"

    class Meta:
        ordering = ['-completed_at']
