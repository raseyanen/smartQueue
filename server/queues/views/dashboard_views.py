"""Dashboard views - личный кабинет пользователя"""

from django.shortcuts import render
from django.contrib.auth.decorators import login_required
from django.db.models import Count, Q
from queues.models import Queue, Ticket, Terminal, Notification


@login_required
def dashboard(request):
    """Личный кабинет пользователя"""
    
    # Мои очереди (где пользователь владелец)
    owned_queues = Queue.objects.filter(owner=request.user).annotate(
        active_tickets_count=Count('tickets', filter=Q(tickets__is_cancelled=False))
    )
    
    # Мои активные талоны
    user_tickets = Ticket.objects.filter(
        user=request.user,
        is_cancelled=False
    ).select_related('queue').order_by('created_at')
    
    # Мои терминалы
    terminals = Terminal.objects.filter(owner=request.user)
    
    # Непрочитанные уведомления
    notifications = Notification.objects.filter(
        user=request.user,
        is_read=False
    )[:5]  # Последние 5
    
    # Статистика
    context = {
        'owned_queues': owned_queues,
        'owned_queues_count': owned_queues.count(),
        'user_tickets': user_tickets,
        'active_tickets_count': user_tickets.count(),
        'terminals': terminals,
        'terminals_count': terminals.count(),
        'notifications': notifications,
        'unread_notifications_count': notifications.count(),
    }
    
    return render(request, 'queues/dashboard.html', context)
