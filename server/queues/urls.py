from django.urls import path
from . import views

app_name = 'queues'

urlpatterns = [
    # Главные страницы
    path('', views.queue_list, name='queue_list'),
    path('create/', views.queue_create, name='queue_create'),

    # Терминалы
    path('terminals/register/', views.terminal_register, name='terminal_register'),
    path('terminals/', views.terminal_list, name='terminal_list'),
    
    # API терминалов
    path('api/terminal/ticket/', views.terminal_get_ticket, name='terminal_get_ticket'),
    path('api/terminal/confirm/', views.terminal_confirm_print, name='terminal_confirm_print'),
    
    # Уведомления
    path('notifications/', views.notifications_list, name='notifications_list'),
    
    # Обмен местами
    path('ticket/<int:ticket_id>/exchange/', views.propose_exchange, name='propose_exchange'),
    path('exchange/<int:exchange_id>/respond/', views.respond_exchange, name='respond_exchange'),
    
    # Бан пользователей
    path('<str:queue_link>/ban/', views.ban_user, name='ban_user'),
    
    # Списки участников
    path('<str:link>/participants/', views.queue_participants_list, name='queue_participants_list'),
    path('<str:link>/finished/', views.queue_finished_list, name='queue_finished_list'),
    
    # Экспорт CSV
    path('<str:link>/export/', views.export_participants_csv, name='export_participants_csv'),
    
    # Детали очереди (ПОСЛЕ всех конкретных путей)
    path('<str:link>/', views.queue_detail, name='queue_detail'),
    path('<str:link>/api/', views.queue_data_api, name='queue_data_api'),
    path('<str:link>/join/', views.join_queue, name='join_queue'),
    path('<str:link>/leave/', views.leave_queue, name='leave_queue'),
    
    # Управление талонами
    path('ticket/<int:ticket_id>/cancel/', views.cancel_ticket, name='cancel_ticket'),
    path('ticket/<int:ticket_id>/delete/', views.delete_ticket, name='delete_ticket'),
]
