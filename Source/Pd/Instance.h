/*
 // Copyright (c) 2015-2022 Pierre Guillot and Timothy Schoen
 // For information on usage and redistribution, and for a DISCLAIMER OF ALL
 // WARRANTIES, see the file, "LICENSE.txt," in this distribution.
 */

#pragma once

extern "C" {
#include <z_libpd.h>
#include <s_inter.h>
}

#include <concurrentqueue.h>

#include "Utility/StringUtils.h"
#include "Patch.h"
#include "Ofelia.h"

class ObjectImplementationManager;

namespace pd {

class Atom {
public:
    // The default constructor.
    inline Atom()
        : type(FLOAT)
        , value(0)
        , symbol()
    {
    }

    static std::vector<pd::Atom> fromAtoms(int ac, t_atom* av)
    {
        auto array = std::vector<pd::Atom>();
        array.reserve(ac);

        for (int i = 0; i < ac; ++i) {
            if (av[i].a_type == A_FLOAT) {
                array.emplace_back(atom_getfloat(av + i));
            } else if (av[i].a_type == A_SYMBOL) {
                array.emplace_back(atom_getsymbol(av + i)->s_name);
            } else {
                array.emplace_back();
            }
        }

        return array;
    }

    // The const floatructor.
    inline Atom(float val)
        : type(FLOAT)
        , value(val)
        , symbol()
    {
    }

    // The string constructor.
    inline Atom(String sym)
        : type(SYMBOL)
        , value(0)
        , symbol(std::move(sym))
    {
    }

    // The pd hash constructor.
    inline Atom(t_symbol* sym)
        : type(SYMBOL)
        , value(0)
        , symbol(String::fromUTF8(sym->s_name))
    {
    }

    // The c-string constructor.
    inline Atom(char const* sym)
        : type(SYMBOL)
        , value(0)
        , symbol(String::fromUTF8(sym))
    {
    }

    // Check if the atom is a float.
    inline bool isFloat() const
    {
        return type == FLOAT;
    }

    // Check if the atom is a string.
    inline bool isSymbol() const
    {
        return type == SYMBOL;
    }

    // Get the float value.
    inline float getFloat() const
    {
        return value;
    }

    // Get the string.
    inline String const& getSymbol() const
    {
        return symbol;
    }

    // Compare two atoms.
    inline bool operator==(Atom const& other) const
    {
        if (type == SYMBOL) {
            return other.type == SYMBOL && symbol == other.symbol;
        } else {
            return other.type == FLOAT && value == other.value;
        }
    }

private:
    enum Type {
        FLOAT,
        SYMBOL
    };
    Type type = FLOAT;
    float value = 0;
    String symbol;
};

class MessageListener;
class Patch;
class Instance {
    struct Message {
        String selector;
        String destination;
        std::vector<pd::Atom> list;
    };

    struct dmessage {

        dmessage(pd::Instance* instance, void* ref, String dest, String sel, std::vector<pd::Atom> atoms)
            : object(ref, instance)
            , destination(dest)
            , selector(sel)
            , list(atoms)
        {
        }

        WeakReference object;
        String destination;
        String selector;
        std::vector<pd::Atom> list;
    };

public:
    Instance(String const& symbol);
    Instance(Instance const& other) = delete;
    virtual ~Instance();

    void initialisePd(String& pdlua_version);
    void prepareDSP(int nins, int nouts, double samplerate, int blockSize);
    void startDSP();
    void releaseDSP();
    void performDSP(float const* inputs, float* outputs);
    int getBlockSize() const;

    void sendNoteOn(int channel, int const pitch, int velocity) const;
    void sendControlChange(int channel, int const controller, int value) const;
    void sendProgramChange(int channel, int value) const;
    void sendPitchBend(int channel, int value) const;
    void sendAfterTouch(int channel, int value) const;
    void sendPolyAfterTouch(int channel, int const pitch, int value) const;
    void sendSysEx(int port, int byte) const;
    void sendSysRealTime(int port, int byte) const;
    void sendMidiByte(int port, int byte) const;

    virtual void receiveNoteOn(int channel, int pitch, int velocity) = 0;
    virtual void receiveControlChange(int channel, int controller, int value) = 0;
    virtual void receiveProgramChange(int channel, int value) = 0;
    virtual void receivePitchBend(int channel, int value) = 0;
    virtual void receiveAftertouch(int channel, int value) = 0;
    virtual void receivePolyAftertouch(int channel, int pitch, int value) = 0;
    virtual void receiveMidiByte(int port, int byte) = 0;

    virtual void createPanel(int type, char const* snd, char const* location, char const* callbackName, int openMode = -1);

    void sendBang(char const* receiver) const;
    void sendFloat(char const* receiver, float value) const;
    void sendSymbol(char const* receiver, char const* symbol) const;
    void sendList(char const* receiver, std::vector<pd::Atom> const& list) const;
    void sendMessage(char const* receiver, char const* msg, std::vector<pd::Atom> const& list) const;
    void sendTypedMessage(void* object, char const* msg, std::vector<Atom> const& list) const;

    virtual void addTextToTextEditor(unsigned long ptr, String text) { }
    virtual void showTextEditor(unsigned long ptr, Rectangle<int> bounds, String title) { }

    virtual void receiveSysMessage(String const& selector, std::vector<pd::Atom> const& list) { }

    void registerMessageListener(void* object, MessageListener* messageListener);
    void unregisterMessageListener(void* object, MessageListener* messageListener);

    void registerWeakReference(void* ptr, pd_weak_reference* ref);
    void unregisterWeakReference(void* ptr, pd_weak_reference const* ref);
    void clearWeakReferences(void* ptr);

    virtual void receiveDSPState(bool dsp) { }

    virtual void updateConsole(int numMessages, bool newWarning) { }

    virtual void titleChanged() { }

    void enqueueFunctionAsync(std::function<void(void)> const& fn);

    void sendDirectMessage(void* object, String const& msg, std::vector<Atom>&& list);
    void sendDirectMessage(void* object, std::vector<pd::Atom>&& list);
    void sendDirectMessage(void* object, String const& msg);
    void sendDirectMessage(void* object, float msg);

    void updateObjectImplementations();
    void clearObjectImplementationsForPatch(pd::Patch* p);

    virtual void performParameterChange(int type, String const& name, float value) { }

    // JYG added this
    virtual void fillDataBuffer(std::vector<pd::Atom> const& list) { }
    virtual void parseDataBuffer(XmlElement const& xml) { }

    void logMessage(String const& message);
    void logError(String const& message);
    void logWarning(String const& message);
    void muteConsole(bool shouldMute);

    std::deque<std::tuple<void*, String, int, int, int>>& getConsoleMessages();
    std::deque<std::tuple<void*, String, int, int, int>>& getConsoleHistory();

    virtual void messageEnqueued() { }

    void sendMessagesFromQueue();
    void processMessage(Message mess);
    void processSend(dmessage mess);

    String getExtraInfo(File const& toOpen);
    Patch::Ptr openPatch(File const& toOpen);

    virtual Colour getForegroundColour() = 0;
    virtual Colour getBackgroundColour() = 0;
    virtual Colour getTextColour() = 0;
    virtual Colour getOutlineColour() = 0;

    virtual void reloadAbstractions(File changedPatch, t_glist* except) = 0;

    void setThis() const;
    t_symbol* generateSymbol(String const& symbol) const;
    t_symbol* generateSymbol(char const* symbol) const;

    void lockAudioThread();
    bool tryLockAudioThread();
    void unlockAudioThread();

    bool loadLibrary(String const& library);

    void* instance = nullptr;
    void* patch = nullptr;
    void* atoms = nullptr;
    void* messageReceiver = nullptr;
    void* parameterReceiver = nullptr;
    void* parameterChangeReceiver = nullptr;
    void* midiReceiver = nullptr;
    void* printReceiver = nullptr;

    // JYG added this
    void* dataBufferReceiver = nullptr;

    std::atomic<bool> canUndo = false;
    std::atomic<bool> canRedo = false;

    inline static const String defaultPatch = "#N canvas 827 239 527 327 12;";

    bool isPerformingGlobalSync = false;
    CriticalSection const audioLock;

private:
    std::mutex weakReferenceMutex;
    std::unordered_map<void*, std::vector<pd_weak_reference*>> pdWeakReferences;
    std::unordered_map<void*, std::vector<juce::WeakReference<MessageListener>>> messageListeners;

    std::unique_ptr<ObjectImplementationManager> objectImplementations;

    CriticalSection messageListenerLock;

    moodycamel::ConcurrentQueue<std::function<void(void)>> functionQueue = moodycamel::ConcurrentQueue<std::function<void(void)>>(4096);

    std::unique_ptr<FileChooser> openChooser;
    std::atomic<bool> consoleMute;

protected:
    struct internal;

    struct ConsoleHandler : public Timer {
        Instance* instance;

        ConsoleHandler(Instance* parent)
            : instance(parent)
            , fastStringWidth(Font(14))
        {
        }

        void timerCallback() override
        {
            auto item = std::tuple<void*, String, bool>();
            int numReceived = 0;
            bool newWarning = false;

            while (pendingMessages.try_dequeue(item)) {
                auto& [object, message, type] = item;
                
                if(consoleMessages.size()) {
                    auto& [lastObject, lastMessage, lastType, lastLength, numMessages] = consoleMessages.back();
                    if(object == lastObject && message == lastMessage && type == lastType) {
                        numMessages++;
                    }
                    else {
                        consoleMessages.emplace_back(object, message, type, fastStringWidth.getStringWidth(message) + 8, 1);
                    }
                }
                else {
                    consoleMessages.emplace_back(object, message, type, fastStringWidth.getStringWidth(message) + 8, 1);
                }
                
                if (consoleMessages.size() > 800)
                    consoleMessages.pop_front();

                numReceived++;
                newWarning = newWarning || type;
            }

            // Check if any item got assigned
            if (numReceived) {
                instance->updateConsole(numReceived, newWarning);
            }

            stopTimer();
        }

        void logMessage(void* object, String const& message)
        {
            pendingMessages.enqueue({ object, message, false });
            startTimer(10);
        }

        void logWarning(void* object, String const& warning)
        {
            pendingMessages.enqueue({ object, warning, 1 });
            startTimer(10);
        }

        void logError(void* object, String const& error)
        {
            pendingMessages.enqueue({ object, error, 2 });
            startTimer(10);
        }

        void processPrint(void* object, char const* message)
        {
            std::function<void(const String)> forwardMessage =
                [this, object](String const& message) {
                    if (message.startsWith("error")) {
                        logError(object, message.substring(7));
                    } else if (message.startsWith("verbose(0):") || message.startsWith("verbose(1):")) {
                        logError(object, message.substring(12));
                    } else {
                        if (message.startsWith("verbose(")) {
                            logMessage(object, message.substring(12));
                        } else {
                            logMessage(object, message);
                        }
                    }
                };

            static int length = 0;
            printConcatBuffer[length] = '\0';

            int len = (int)strlen(message);
            while (length + len >= 2048) {
                int d = 2048 - 1 - length;
                strncat(printConcatBuffer, message, d);

                // Send concatenated line to plugdata!
                forwardMessage(String::fromUTF8(printConcatBuffer));

                message += d;
                len -= d;
                length = 0;
                printConcatBuffer[0] = '\0';
            }

            strncat(printConcatBuffer, message, len);
            length += len;

            if (length > 0 && printConcatBuffer[length - 1] == '\n') {
                printConcatBuffer[length - 1] = '\0';

                // Send concatenated line to plugdata!
                forwardMessage(String::fromUTF8(printConcatBuffer));

                length = 0;
            }
        }

        std::deque<std::tuple<void*, String, int, int, int>> consoleMessages;
        std::deque<std::tuple<void*, String, int, int, int>> consoleHistory;

        char printConcatBuffer[2048];

        moodycamel::ConcurrentQueue<std::tuple<void*, String, bool>> pendingMessages;

        StringUtils fastStringWidth; // For formatting console messages more quickly
    };

    std::unique_ptr<Ofelia> ofelia;

    ConsoleHandler consoleHandler;
    
    JUCE_DECLARE_WEAK_REFERENCEABLE (Instance)
};
} // namespace pd
