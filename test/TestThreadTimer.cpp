#include "ThreadTimer.h"

using namespace other::time;
using namespace other::time::timer;

// 1. Создаем действие, которое выполнится по таймеру
class NotificationTask : public ICallable 
{
private:
    std::string _message;

public:
    NotificationTask(const std::string& msg) : _message(msg) {}

    // Этот метод будет вызван ФОНОВЫМ потоком контроллера
    void callAfterTimeout() override 
    {
        std::cout << "\n[ТАЙМЕР СРАБОТАЛ]: " << _message << std::endl;
        std::cout << "[ПОТОК]: ID фонового потока = " << std::this_thread::get_id() << std::endl;
        std::cout << "------------------------------------------" << std::endl;
    }
};

int main() 
{
    setlocale(LC_ALL, "Russian");

    // 2. Инициализация
    auto _timer = std::make_shared<Timer>();
    ThreadTimer timerController(_timer);

    // 3. Запускаем фоновый цикл проверки (интервал проверки каждые 100 мс)
    // Теперь система "ожила" и ждет появления таймеров.
    timerController.Start(100);

    std::cout << "Система запущена. Основной поток ID: " << std::this_thread::get_id() << std::endl;

    // 4. Подготовка параметров для циклического таймера
    DateTime start = DateTime::Now();
    DateTime firstTrigger = start + TimeSpan::FromSeconds(3); // Первая сработка через 3 сек
    TimeSpan interval = TimeSpan::FromSeconds(2);           // Повтор каждые 2 сек или (TimeSpan::Zero без повторов)

    // Создаем объект с нашей логикой
    auto myTask = std::make_shared<NotificationTask>("Пора сделать разминку!");

    // 5. Регистрация таймера в системе
    // CreateTimer возвращает ID, по которому мы сможем управлять этим конкретным таймером
    uint64_t taskID = timerController.CreateTimer(
        start,
        firstTrigger,
        interval,
        myTask,
        "HealthCheck"
    );

    // 6. Активация таймера (после создания он всегда на паузе для безопасности)
    timerController.StartTimer(taskID);
    std::cout << "Таймер [HealthCheck] создан с ID: " << taskID << ". Ждем 3 секунды..." << std::endl;

    // Имитация работы основного потока
    for (int i = 0; i < 10; ++i) {
        // Выводим оставшееся время до ближайшего срабатывания
        TimeSpan remaining = timerController.RemainingTime(taskID);
        std::cout << "До срабатывания осталось: " << remaining.TotalSeconds() << " сек.  \r" << std::flush;

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        // Демонстрация паузы на 5-й секунде
        if (i == 4) {
            std::cout << "\n[INFO] Ставим таймер на паузу на 2 секунды..." << std::endl;
            timerController.PauseTimer(taskID);
            std::this_thread::sleep_for(std::chrono::seconds(2));
            std::cout << "[INFO] Возобновляем работу таймера." << std::endl;
            timerController.ResumeTimer(taskID);
        }
    }

    // 7. Завершение работы
    std::cout << "\nОстановка системы таймеров..." << std::endl;

    // Метод Stop() остановит фоновый поток и дождется его завершения
    timerController.Stop();

    std::cout << "Программа успешно завершена." << std::endl;
    return 0;
}

