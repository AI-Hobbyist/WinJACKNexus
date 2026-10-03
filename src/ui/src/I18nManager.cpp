/* SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/UI/I18nManager.hpp>
#include <WinJACKNexus/Common/I18nApi.h>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStringDecoder>
#include <QThread>
#include <Windows.h>
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>

namespace WinJACKNexus::UI {
namespace {
constexpr qsizetype packageLimit = 2 * 1024 * 1024;
constexpr qsizetype argumentLimit = 64 * 1024;
QPointer<I18nManager> installedManager;
QHash<quintptr,QPointer<I18nManager>> libraryOwners;
bool fail(QString* error, const QString& message) { if (error) *error = message; return false; }
bool identifier(const QString& name) {
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z][A-Za-z0-9_.-]{0,255}$"));
    return pattern.match(name).hasMatch();
}
bool guiThread(const QObject* object) {
    return QCoreApplication::instance() && object->thread()==QCoreApplication::instance()->thread()
        && QThread::currentThread()==object->thread();
}
bool strictJson(const QByteArray& bytes, QJsonObject* result, QString* error,
                qsizetype limit = packageLimit) {
    if (bytes.isEmpty() || bytes.size() > limit) return fail(error, QStringLiteral("Invalid JSON size"));
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    const QString decoded = decoder(bytes);
    if (decoder.hasError() || decoded.contains(QChar::Null))
        return fail(error, QStringLiteral("Invalid UTF-8 or NUL"));
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(error, QStringLiteral("Invalid JSON object: ") + parseError.errorString());
    // Qt resolves duplicate object keys by overwriting. Reject them before use,
    // including differently escaped spellings of the same key.
    struct Frame { bool object; bool key; QSet<QString> keys; };
    QList<Frame> stack;
    for (qsizetype i = 0; i < bytes.size(); ++i) {
        const char c = bytes[i];
        if (c == '"') {
            const auto start = i++;
            while (i < bytes.size()) {
                if (bytes[i] == '\\') { i += 2; continue; }
                if (bytes[i] == '"') break;
                ++i;
            }
            const auto stringDoc = QJsonDocument::fromJson("[" + bytes.mid(start, i-start+1) + "]");
            const QString token = stringDoc.array().at(0).toString();
            for (const QChar ch : token)
                if (ch == QChar::Null) return fail(error, QStringLiteral("JSON strings must not contain NUL"));
            if (!stack.isEmpty() && stack.last().object && stack.last().key) {
                if (stack.last().keys.contains(token)) return fail(error, QStringLiteral("Duplicate JSON key: ") + token);
                stack.last().keys.insert(token);
                stack.last().key = false;
            }
        } else if (c == '{' || c == '[') {
            stack.append({c == '{', c == '{', {}});
            if (stack.size() > 64) return fail(error, QStringLiteral("JSON nesting limit exceeded"));
        } else if (c == '}' || c == ']') stack.removeLast();
        else if (c == ',' && !stack.isEmpty() && stack.last().object) stack.last().key = true;
    }
    *result = document.object();
    return true;
}
bool onlyFields(const QJsonObject& object, const QStringList& fields, QString* error) {
    for (auto it = object.begin(); it != object.end(); ++it)
        if (!fields.contains(it.key())) return fail(error, QStringLiteral("Unknown field: ") + it.key());
    return true;
}
bool positiveVersion(const QJsonValue& value) {
    return value.isDouble() && value.toDouble() >= 1 && value.toDouble() <= 2147483647
        && value.toDouble() == value.toInt();
}
bool domainList(const QJsonValue& value, QStringList* result, QString* error) {
    if (!value.isArray() || value.toArray().isEmpty()) return fail(error, QStringLiteral("Missing domains"));
    for (const auto& item : value.toArray()) {
        const auto name = item.toString();
        if (!item.isString() || !identifier(name) || result->contains(name))
            return fail(error, QStringLiteral("Invalid or duplicate domain"));
        result->append(name);
    }
    result->sort();
    return true;
}
QString confinedPath(const QString& root, const QString& relative, QString* error) {
    const auto normalized = QDir::fromNativeSeparators(relative);
    if (normalized.isEmpty() || QDir::isAbsolutePath(normalized) || normalized.contains(QLatin1Char(':'))
        || normalized.split(QLatin1Char('/')).contains(QStringLiteral(".."))) {
        fail(error, QStringLiteral("Invalid relative language resource path")); return {};
    }
    const auto path = QFileInfo(QDir(root).filePath(normalized)).canonicalFilePath();
    if (path.isEmpty()) { fail(error, QStringLiteral("Language resource does not exist")); return {}; }
    if (!path.startsWith(root + QLatin1Char('/'), Qt::CaseInsensitive)) {
        fail(error, QStringLiteral("Language resource escapes module root")); return {};
    }
    return path;
}
QByteArray readResource(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > packageLimit) {
        fail(error, QStringLiteral("Cannot read language resource: ") + path); return {};
    }
    return file.read(packageLimit+1);
}
bool tokens(const QString& source, QSet<QString>* names, QString* error) {
    for (qsizetype i = 0; i < source.size(); ++i) {
        if (source[i] == QLatin1Char('{')) {
            if (i+1 < source.size() && source[i+1] == QLatin1Char('{')) { ++i; continue; }
            const auto end = source.indexOf(QLatin1Char('}'), i+1);
            if (end < 0) return fail(error, QStringLiteral("Unclosed placeholder"));
            const auto name = source.mid(i+1, end-i-1);
            static const QRegularExpression pattern(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
            if (!pattern.match(name).hasMatch()) return fail(error, QStringLiteral("Invalid placeholder"));
            names->insert(name); i = end;
        } else if (source[i] == QLatin1Char('}')) {
            if (i+1 < source.size() && source[i+1] == QLatin1Char('}')) ++i;
            else return fail(error, QStringLiteral("Unmatched closing brace"));
        }
    }
    return true;
}
bool forms(const QJsonValue& value, const QString& language, QStringList* output, QString* error) {
    if (value.isString()) output->append(value.toString());
    else if (value.isObject()) {
        const auto object = value.toObject();
        const QStringList expected = language == QStringLiteral("en-US")
            ? QStringList{QStringLiteral("one"),QStringLiteral("other")} : QStringList{QStringLiteral("other")};
        if (!onlyFields(object, expected, error) || object.size() != expected.size())
            return fail(error, QStringLiteral("Invalid plural forms"));
        for (const auto& key : expected) {
            if (!object[key].isString()) return fail(error, QStringLiteral("Plural form must be text"));
            output->append(object[key].toString());
        }
    } else return fail(error, QStringLiteral("Translation must be text or plural object"));
    QSet<QString> baseline;
    bool first = true;
    for (const auto& form : *output) {
        QSet<QString> names;
        if (form.trimmed().isEmpty() || form.size() > 32768 || !tokens(form,&names,error))
            return fail(error, QStringLiteral("Empty, oversized or malformed translation"));
        if (!first && names != baseline) return fail(error, QStringLiteral("Plural placeholder mismatch"));
        baseline = names;
        first = false;
    }
    return true;
}
bool catalog(const QByteArray& bytes, const QString& language, const QJsonObject& manifest,
             const QJsonObject& english, QJsonObject* result, QString* error) {
    QJsonObject document;
    if (!strictJson(bytes,&document,error) || !onlyFields(document,
        {QStringLiteral("formatVersion"),QStringLiteral("catalogVersion"),QStringLiteral("moduleId"),
         QStringLiteral("language"),QStringLiteral("domains")},error)) return false;
    if (document[QStringLiteral("formatVersion")].toDouble() != 1
        || document[QStringLiteral("language")].toString() != language
        || document[QStringLiteral("moduleId")] != manifest[QStringLiteral("moduleId")]
        || document[QStringLiteral("catalogVersion")] != manifest[QStringLiteral("catalogVersion")]
        || !document[QStringLiteral("domains")].isObject())
        return fail(error, QStringLiteral("Language catalog identity/version mismatch"));
    const auto domains = document[QStringLiteral("domains")].toObject();
    const auto required = manifest[QStringLiteral("requiredKeys")].toObject();
    if (language == QStringLiteral("en-US") && domains.size() != required.size())
        return fail(error, QStringLiteral("Incomplete English domains"));
    for (auto domain = domains.begin(); domain != domains.end(); ++domain) {
        if (!required.contains(domain.key()) || !domain.value().isObject())
            return fail(error, QStringLiteral("Unknown domain or malformed entries"));
        const auto entries = domain.value().toObject();
        const auto keys = required[domain.key()].toArray();
        if (language == QStringLiteral("en-US") && entries.size() != keys.size())
            return fail(error, QStringLiteral("Incomplete English keys"));
        for (auto entry = entries.begin(); entry != entries.end(); ++entry) {
            if (!keys.contains(entry.key())) return fail(error, QStringLiteral("Unknown translation key: ") + entry.key());
            QStringList textForms;
            if (!forms(entry.value(),language,&textForms,error)) return false;
            if (language != QStringLiteral("en-US")) {
                const QJsonValue reference = english[domain.key()].toObject()[entry.key()];
                QStringList referenceForms;
                if (entry.value().isObject() != reference.isObject()
                    || !forms(reference,QStringLiteral("en-US"),&referenceForms,error))
                    return fail(error, QStringLiteral("Translation plural type mismatch"));
                QSet<QString> names, referenceNames;
                tokens(textForms.first(),&names,error);
                tokens(referenceForms.first(),&referenceNames,error);
                if (names != referenceNames) return fail(error, QStringLiteral("Translation placeholder mismatch: ") + entry.key());
            }
        }
    }
    *result = domains;
    return true;
}
QString selectForm(const QJsonValue& value, const QString& language, qint64 count) {
    if (value.isString()) return value.toString();
    return value.toObject()[language == QStringLiteral("en-US") && count == 1
        ? QStringLiteral("one") : QStringLiteral("other")].toString();
}
}

struct I18nManager::Impl {
    struct Context { QPointer<I18nManager> manager; QStringList domains; };
    struct Module {
        QString id, root, locate;
        QStringList domains;
        QJsonObject manifest, descriptor;
        QByteArray embedded, embeddedManifest;
        int references = 1;
        HMODULE library = nullptr;
        WjnI18nChangedFn changed = nullptr;
        WjnI18nDetachFn detach = nullptr;
        Context context;
        WjnI18nHostV1 services {};
        ~Module() {
            if (detach) detach();
            if (library) { libraryOwners.remove(reinterpret_cast<quintptr>(library)); FreeLibrary(library); }
        }
    };
    struct Catalogs { QJsonObject english, current; };
    struct Snapshot {
        QString selection = QStringLiteral("auto"), language = QStringLiteral("en-US");
        quint64 revision = 0;
        QHash<QString,Catalogs> domains;
    };
    I18nManager* owner;
    I18nOptions options;
    std::map<QString,std::unique_ptr<Module>> modules;
    std::atomic<std::shared_ptr<const Snapshot>> snapshot;
    mutable std::mutex logMutex;
    mutable QSet<QString> logged;
    bool publishing = false;
    explicit Impl(I18nManager* manager, I18nOptions value) : owner(manager),options(std::move(value)) {}
    std::unique_ptr<QSettings> settings() const {
        if (!options.settingsFile.isEmpty()) return std::make_unique<QSettings>(options.settingsFile,QSettings::IniFormat);
        return std::make_unique<QSettings>(QStringLiteral("WinJACKNexus"),QStringLiteral("Application"));
    }
    void log(const QString& code, const QString& detail) const {
        { std::lock_guard lock(logMutex);
          if (logged.contains(code+QLatin1Char('/')+detail)) return;
          logged.insert(code+QLatin1Char('/')+detail); }
        Q_EMIT owner->diagnostic(code,detail);
        qWarning().noquote() << code << detail;
    }
    bool mutation(QString* error) const {
        if (!guiThread(owner)) return fail(error,QStringLiteral("i18n mutation requires GUI thread"));
        if (publishing) return fail(error,QStringLiteral("i18n mutation during snapshot notification"));
        return true;
    }
    QString automatic() const {
        return I18nManager::resolveAutomatic(options.systemUiLanguages.isEmpty()
            ? QLocale::system().uiLanguages() : options.systemUiLanguages);
    }
    bool load(Module& module, const QString& language, Catalogs* output, bool strict, QString* error) {
        const auto mappings = module.manifest[QStringLiteral("languages")].toObject();
        auto read = [&](const QString& locale, bool* exists) {
            *exists = false;
            if (!mappings.contains(locale)) return QByteArray{};
            const auto relative = module.locate+QLatin1Char('/')+mappings[locale].toString();
            if (!QFileInfo::exists(QDir(module.root).filePath(relative))) return QByteArray{};
            *exists = true;
            const auto path = confinedPath(module.root,relative,error);
            return path.isEmpty() ? QByteArray{} : readResource(path,error);
        };
        bool exists;
        const auto englishBytes = read(QStringLiteral("en-US"),&exists);
        QJsonObject embeddedCatalog;
        if (!module.embedded.isEmpty()
            && !catalog(module.embedded,QStringLiteral("en-US"),module.manifest,{},&embeddedCatalog,error)) return false;
        QString englishError;
        const bool externalValid = catalog(englishBytes,QStringLiteral("en-US"),module.manifest,{},&output->english,&englishError);
        if (!externalValid || (!embeddedCatalog.isEmpty() && embeddedCatalog != output->english)) {
            if (embeddedCatalog.isEmpty()) return fail(error,QStringLiteral("Complete English required: ")+englishError);
            output->english = embeddedCatalog;
            log(QStringLiteral("I18N_ENGLISH_FALLBACK"),module.id);
        }
        output->current = {};
        if (language == QStringLiteral("en-US")) return true;
        const auto currentBytes = read(language,&exists);
        if (!exists) { log(QStringLiteral("I18N_LANGUAGE_FALLBACK"),module.id+QLatin1Char('/')+language); return true; }
        QString targetError;
        if (!catalog(currentBytes,language,module.manifest,output->english,&output->current,&targetError)) {
            if (strict) return fail(error,module.id+QStringLiteral(": ")+targetError);
            log(QStringLiteral("I18N_INVALID_LANGUAGE_FALLBACK"),module.id+QLatin1Char('/')+language);
            output->current = {};
        }
        return true;
    }
    void publish(std::shared_ptr<Snapshot> next) {
        snapshot.store(next);
        publishing = true;
        for (const auto& [id,module] : modules)
            if (module->changed) module->changed(next->language.toUtf8().constData(),next->revision);
        Q_EMIT owner->languageChanged(next->language,next->revision);
        publishing = false;
    }
    static int32_t abiText(void* opaque, const char* domain, const char* key,
        const char* fallback, const char* arguments, int64_t count, char* buffer,
        uint32_t capacity, uint32_t* required) {
        try {
            if (required) *required = 0;
            auto* context = static_cast<Context*>(opaque);
            if (!context || !context->manager || !domain || !key || !fallback || !required
                || (!buffer && capacity) || count < -1) return WJN_I18N_INVALID_ARGUMENT;
            if (!guiThread(context->manager)) return WJN_I18N_WRONG_THREAD;
            auto string = [](const char* value, qsizetype limit, QString* output) {
                const auto length = strnlen_s(value,static_cast<size_t>(limit+1));
                if (length > static_cast<size_t>(limit)) return false;
                QStringDecoder decoder(QStringDecoder::Utf8,QStringConverter::Flag::Stateless);
                *output = decoder(QByteArrayView(value,static_cast<qsizetype>(length)));
                return !decoder.hasError();
            };
            QString d,k,f;
            if (!string(domain,256,&d) || !string(key,256,&k) || !string(fallback,32768,&f)
                || !identifier(d) || !identifier(k)) return WJN_I18N_INVALID_ARGUMENT;
            if (!context->domains.contains(d)) return WJN_I18N_FORBIDDEN_DOMAIN;
            QVariantMap args;
            if (arguments) {
                const auto size = strnlen_s(arguments,argumentLimit+1);
                if (size > argumentLimit) return WJN_I18N_INVALID_ARGUMENT;
                QJsonObject object;
                if (!strictJson(QByteArray(arguments,static_cast<qsizetype>(size)),&object,nullptr,argumentLimit))
                    return WJN_I18N_INVALID_ARGUMENT;
                for (auto it = object.begin(); it != object.end(); ++it) {
                    if (!(it.value().isString() || it.value().isDouble() || it.value().isBool())) return WJN_I18N_INVALID_ARGUMENT;
                    args.insert(it.key(),it.value().toVariant());
                }
            }
            QString formatted;
            if (!I18nManager::format(f,args,count,&formatted)) return WJN_I18N_INVALID_ARGUMENT;
            const auto bytes = context->manager->text(d,k,f,args,count).toUtf8();
            *required = static_cast<uint32_t>(bytes.size()+1);
            if (!buffer && !capacity) return WJN_I18N_OK;
            if (capacity < *required) return WJN_I18N_BUFFER_TOO_SMALL;
            std::memcpy(buffer,bytes.constData(),bytes.size()); buffer[bytes.size()] = '\0';
            return WJN_I18N_OK;
        } catch (...) { return WJN_I18N_INTERNAL_ERROR; }
    }
};

I18nDomainLease::I18nDomainLease() = default;
I18nDomainLease::I18nDomainLease(I18nManager* manager, QString id) : manager_(manager), moduleId_(std::move(id)) {}
I18nDomainLease::~I18nDomainLease() { reset(); }
I18nDomainLease::I18nDomainLease(I18nDomainLease&& other) noexcept
    : manager_(other.manager_),moduleId_(std::move(other.moduleId_)) { other.manager_.clear(); }
I18nDomainLease& I18nDomainLease::operator=(I18nDomainLease&& other) noexcept {
    if (this != &other) { reset(); manager_=other.manager_; moduleId_=std::move(other.moduleId_); other.manager_.clear(); }
    return *this;
}
I18nDomainLease::operator bool() const { return !manager_.isNull(); }
void I18nDomainLease::reset() { if (manager_) manager_->release(moduleId_); manager_.clear(); moduleId_.clear(); }

I18nManager::I18nManager(I18nOptions options, QObject* parent)
    : QObject(parent),impl_(std::make_unique<Impl>(this,std::move(options))) {
    auto next = std::make_shared<Impl::Snapshot>();
    auto store = impl_->settings();
    next->selection = store->value(QStringLiteral("i18n/language"),QStringLiteral("auto")).toString();
    if (next->selection != QStringLiteral("auto") && !supportedLanguages().contains(next->selection)) {
        impl_->log(QStringLiteral("I18N_INVALID_SAVED_LANGUAGE"),next->selection);
        next->selection = QStringLiteral("auto");
        store->setValue(QStringLiteral("i18n/language"),next->selection); store->sync();
    }
    next->language = next->selection == QStringLiteral("auto") ? impl_->automatic() : next->selection;
    impl_->snapshot.store(next);
}
I18nManager::~I18nManager() {
    if (installedManager == this) installedManager.clear();
    impl_->modules.clear(); // detach while host services and manager still exist
}
I18nManager* I18nManager::installed() { return installedManager.data(); }
bool I18nManager::install(QString* error) {
    if (!impl_->mutation(error)) return false;
    if (installedManager && installedManager != this) return fail(error,QStringLiteral("Another i18n manager is installed"));
    installedManager=this; return true;
}
QStringList I18nManager::supportedLanguages() { return {QStringLiteral("zh-CN"),QStringLiteral("en-US"),QStringLiteral("ja-JP"),QStringLiteral("ko-KR")}; }
QString I18nManager::resolveAutomatic(const QStringList& preferences) {
    static const QRegularExpression locale(QStringLiteral("^[A-Za-z]{2,3}(?:[-_][A-Za-z0-9]{2,8})*$"));
    for (const auto& value : preferences) {
        if (!locale.match(value).hasMatch()) continue;
        const auto primary = value.split(QRegularExpression(QStringLiteral("[-_]"))).first().toLower();
        if (primary == QStringLiteral("zh")) return QStringLiteral("zh-CN");
        if (primary == QStringLiteral("en")) return QStringLiteral("en-US");
        if (primary == QStringLiteral("ja")) return QStringLiteral("ja-JP");
        if (primary == QStringLiteral("ko")) return QStringLiteral("ko-KR");
    }
    return QStringLiteral("en-US");
}
QString I18nManager::selectedLanguage() const { return impl_->snapshot.load()->selection; }
QString I18nManager::effectiveLanguage() const { return impl_->snapshot.load()->language; }
quint64 I18nManager::revision() const { return impl_->snapshot.load()->revision; }
bool I18nManager::setLanguage(const QString& selection, QString* error) {
    if (!impl_->mutation(error)) return false;
    if (selection != QStringLiteral("auto") && !supportedLanguages().contains(selection)) return fail(error,QStringLiteral("Unsupported language selection"));
    const auto previous = impl_->snapshot.load();
    auto next = std::make_shared<Impl::Snapshot>();
    next->selection=selection; next->language=selection == QStringLiteral("auto") ? impl_->automatic() : selection;
    next->revision=previous->revision+1;
    for (const auto& [id,module] : impl_->modules) {
        Impl::Catalogs data;
        if (!impl_->load(*module,next->language,&data,true,error)) return false;
        for (const auto& domain : module->domains) next->domains.insert(domain,data);
    }
    auto store = impl_->settings();
    store->setValue(QStringLiteral("i18n/language"),selection); store->sync();
    if (store->status() != QSettings::NoError) {
        store->setValue(QStringLiteral("i18n/language"),previous->selection);
        return fail(error,QStringLiteral("Unable to persist language selection"));
    }
    impl_->publish(next); return true;
}
I18nDomainLease I18nManager::registerModule(const QJsonObject& descriptor, const QString& root,
        const QByteArray& embedded, QString* error) {
    return registerModuleImpl(descriptor,root,embedded,{},error);
}
I18nDomainLease I18nManager::registerBundledModule(const QJsonObject& descriptor, const QString& root,
        const QByteArray& embeddedManifest, const QByteArray& embeddedEnglish, QString* error) {
    return registerModuleImpl(descriptor,root,embeddedEnglish,embeddedManifest,error);
}
I18nDomainLease I18nManager::registerModuleImpl(const QJsonObject& descriptor, const QString& root,
        const QByteArray& embedded, const QByteArray& embeddedManifest, QString* error) {
    if (!impl_->mutation(error)) return {};
    const auto canonical = QFileInfo(root).canonicalFilePath();
    const auto id = descriptor[QStringLiteral("moduleId")].toString();
    const auto locate = descriptor[QStringLiteral("locatePath")].toString();
    QStringList domains;
    if (!QDir::isAbsolutePath(root) || canonical.isEmpty() || !QFileInfo(canonical).isDir()
        || !identifier(id) || descriptor[QStringLiteral("descriptorVersion")].toDouble() != 1
        || descriptor[QStringLiteral("i18nApiVersion")].toDouble() != 1
        || !positiveVersion(descriptor[QStringLiteral("catalogVersion")])
        || !domainList(descriptor[QStringLiteral("domains")],&domains,error)
        || !onlyFields(descriptor,{QStringLiteral("descriptorVersion"),QStringLiteral("i18nApiVersion"),
            QStringLiteral("moduleId"),QStringLiteral("domains"),QStringLiteral("locatePath"),
            QStringLiteral("catalogVersion"),QStringLiteral("displayNameKey")},error)) {
        fail(error,QStringLiteral("Invalid module descriptor/root")); return {};
    }
    if (locate != QStringLiteral("locate")) { fail(error,QStringLiteral("locatePath must be locate")); return {}; }
    if (auto found = impl_->modules.find(id); found != impl_->modules.end()) {
        auto& module=*found->second;
        auto normalized=descriptor; normalized.remove(QStringLiteral("displayNameKey"));
        auto old=module.descriptor; old.remove(QStringLiteral("displayNameKey"));
        if (module.root!=canonical || normalized!=old || embedded!=module.embedded || embeddedManifest!=module.embeddedManifest) {
            fail(error,QStringLiteral("Conflicting module registration")); return {};
        }
        ++module.references; return I18nDomainLease(this,id);
    }
    for (const auto& [otherId,module] : impl_->modules)
        for (const auto& domain : domains)
            if (module->domains.contains(domain)) { fail(error,QStringLiteral("Domain already owned: ")+domain); return {}; }
    const auto manifestPath=confinedPath(canonical,locate+QStringLiteral("/manifest.json"),error);
    QJsonObject manifest;
    const bool validExternal = !manifestPath.isEmpty() && strictJson(readResource(manifestPath,error),&manifest,error);
    if (!embeddedManifest.isEmpty()) {
        QJsonObject trusted;
        if (!strictJson(embeddedManifest,&trusted,error)) return {};
        if (!validExternal || manifest!=trusted) impl_->log(QStringLiteral("I18N_MANIFEST_FALLBACK"),id);
        manifest=trusted;
        if (error) error->clear();
    } else if (!validExternal) return {};
    QStringList declaredDomains;
    if (!onlyFields(manifest,{QStringLiteral("formatVersion"),QStringLiteral("moduleId"),QStringLiteral("domains"),
            QStringLiteral("defaultLanguage"),QStringLiteral("catalogVersion"),QStringLiteral("languages"),QStringLiteral("requiredKeys")},error)
        || manifest[QStringLiteral("formatVersion")].toDouble()!=1
        || manifest[QStringLiteral("moduleId")].toString()!=id
        || manifest[QStringLiteral("catalogVersion")]!=descriptor[QStringLiteral("catalogVersion")]
        || manifest[QStringLiteral("defaultLanguage")].toString()!=QStringLiteral("en-US")
        || !domainList(manifest[QStringLiteral("domains")],&declaredDomains,error) || declaredDomains!=domains
        || !manifest[QStringLiteral("languages")].isObject() || !manifest[QStringLiteral("requiredKeys")].isObject()) {
        fail(error,QStringLiteral("Invalid manifest identity/schema")); return {};
    }
    const auto required = manifest[QStringLiteral("requiredKeys")].toObject();
    if (required.size()!=domains.size()) { fail(error,QStringLiteral("requiredKeys must cover all domains")); return {}; }
    for (const auto& domain : domains) {
        QSet<QString> keys;
        if (!required[domain].isArray() || required[domain].toArray().isEmpty()) { fail(error,QStringLiteral("Empty requiredKeys")); return {}; }
        for (const auto& value : required[domain].toArray()) {
            const auto key=value.toString();
            if (!identifier(key) || keys.contains(key)) { fail(error,QStringLiteral("Invalid or duplicate required key")); return {}; }
            keys.insert(key);
        }
    }
    if (descriptor.contains(QStringLiteral("displayNameKey"))) {
        const auto display=descriptor[QStringLiteral("displayNameKey")].toObject();
        if (!onlyFields(display,{QStringLiteral("domain"),QStringLiteral("key")},error)
            || !required[display[QStringLiteral("domain")].toString()].toArray().contains(display[QStringLiteral("key")].toString())) {
            fail(error,QStringLiteral("Invalid displayNameKey")); return {};
        }
    }
    const auto mappings=manifest[QStringLiteral("languages")].toObject();
    if (!mappings.contains(QStringLiteral("en-US"))) { fail(error,QStringLiteral("Manifest must map English")); return {}; }
    QSet<QString> paths;
    for (auto mapping=mappings.begin(); mapping!=mappings.end(); ++mapping) {
        const auto path=mapping.value().toString();
        if (!supportedLanguages().contains(mapping.key()) || !mapping.value().isString()
            || path!=mapping.key()+QStringLiteral(".json") || paths.contains(path.toLower())) {
            fail(error,QStringLiteral("Invalid language mapping")); return {};
        }
        paths.insert(path.toLower());
    }
    auto module=std::make_unique<Impl::Module>();
    module->id=id; module->root=canonical; module->locate=locate; module->domains=domains;
    module->manifest=manifest; module->descriptor=descriptor; module->embedded=embedded;
    module->embeddedManifest=embeddedManifest;
    const auto previous=impl_->snapshot.load();
    Impl::Catalogs data;
    if (!impl_->load(*module,previous->language,&data,false,error)) return {};
    auto next=std::make_shared<Impl::Snapshot>(*previous); ++next->revision;
    for (const auto& domain : domains) next->domains.insert(domain,data);
    impl_->modules.emplace(id,std::move(module)); impl_->publish(next);
    return I18nDomainLease(this,id);
}
I18nDomainLease I18nManager::registerLibrary(const QString& path, Extension extension, quint32 index, QString* error) {
    if (!impl_->mutation(error)) return {};
    const auto canonical=QFileInfo(path).canonicalFilePath();
    if (!QDir::isAbsolutePath(path) || canonical.isEmpty()) { fail(error,QStringLiteral("Absolute DLL path required")); return {}; }
    HMODULE library=LoadLibraryW(reinterpret_cast<LPCWSTR>(canonical.utf16()));
    if (!library) { fail(error,QStringLiteral("Cannot load i18n module DLL")); return {}; }
    struct Guard { HMODULE value; ~Guard() { if(value) FreeLibrary(value); } } guard{library};
    const auto owner=libraryOwners.value(reinterpret_cast<quintptr>(library));
    if (owner && owner!=this) { fail(error,QStringLiteral("DLL is attached to another i18n host")); return {}; }
    const auto version=reinterpret_cast<WjnI18nVersionFn>(GetProcAddress(library,extension==Extension::node
        ? "wjn_get_node_i18n_api_version" : "wjn_fx_i18n_api_version"));
    const auto descriptorFn=reinterpret_cast<WjnI18nDescriptorFn>(GetProcAddress(library,extension==Extension::node
        ? "wjn_get_node_i18n_descriptor_json" : "wjn_fx_i18n_descriptor_json"));
    if (!version && !descriptorFn) { fail(error,QStringLiteral("Module has no optional i18n extension")); return {}; }
    if (!version || !descriptorFn || version()!=WJN_I18N_API_VERSION) { fail(error,QStringLiteral("Unsupported i18n extension")); return {}; }
    const char* json=descriptorFn(index);
    if (!json || strnlen_s(json,packageLimit+1)>packageLimit) { fail(error,QStringLiteral("Invalid i18n descriptor index/size")); return {}; }
    QJsonObject descriptor;
    if (!strictJson(QByteArray(json),&descriptor,error)) return {};
    for (const auto& [id,module] : impl_->modules)
        if (module->library==library && id!=descriptor[QStringLiteral("moduleId")].toString()) {
            fail(error,QStringLiteral("One DLL must use one module identity")); return {};
        }
    const auto attach=reinterpret_cast<WjnI18nAttachFn>(GetProcAddress(library,"wjn_i18n_attach_host"));
    const auto changed=reinterpret_cast<WjnI18nChangedFn>(GetProcAddress(library,"wjn_i18n_language_changed"));
    const auto detach=reinterpret_cast<WjnI18nDetachFn>(GetProcAddress(library,"wjn_i18n_detach_host"));
    if ((attach || changed || detach) && !(attach && changed && detach)) { fail(error,QStringLiteral("Incomplete i18n lifecycle exports")); return {}; }
    auto lease=registerModule(descriptor,QFileInfo(canonical).absolutePath(),{},error);
    if (!lease) return {};
    auto& module=*impl_->modules.at(descriptor[QStringLiteral("moduleId")].toString());
    if (module.library) {
        if (module.library!=library) { lease.reset(); fail(error,QStringLiteral("Module ID belongs to another DLL")); return {}; }
        return lease;
    }
    module.library=library; guard.value=nullptr;
    libraryOwners.insert(reinterpret_cast<quintptr>(library),this);
    module.context={this,module.domains};
    module.services={sizeof(WjnI18nHostV1),WJN_I18N_API_VERSION,&module.context,&Impl::abiText};
    if (attach && attach(&module.services)!=WJN_I18N_OK) {
        detach();
        libraryOwners.remove(reinterpret_cast<quintptr>(library));
        module.library=nullptr; module.services={}; module.context={};
        FreeLibrary(library);
        lease.reset(); fail(error,QStringLiteral("Module rejected i18n host")); return {};
    }
    module.detach=detach; module.changed=changed;
    if (changed) changed(effectiveLanguage().toUtf8().constData(),revision());
    return lease;
}
void I18nManager::release(const QString& id) {
    if (!guiThread(this) || impl_->publishing) {
        // Queue last-reference cleanup; callbacks finish before detach/unload.
        QMetaObject::invokeMethod(this,[this,id] { release(id); },Qt::QueuedConnection); return;
    }
    auto found=impl_->modules.find(id);
    if (found==impl_->modules.end() || --found->second->references) return;
    auto next=std::make_shared<Impl::Snapshot>(*impl_->snapshot.load()); ++next->revision;
    for (const auto& domain : found->second->domains) next->domains.remove(domain);
    impl_->modules.erase(found); impl_->publish(next);
}
bool I18nManager::format(const QString& source, const QVariantMap& arguments, qint64 count, QString* result, QString* error) {
    if (!result || count < -1) return fail(error,QStringLiteral("Invalid formatter arguments"));
    QSet<QString> names;
    if (!tokens(source,&names,error)) return false;
    QVariantMap values=arguments;
    if (count >= 0) values.insert(QStringLiteral("count"),count);
    for (const auto& name : names) {
        if (!values.contains(name)) return fail(error,QStringLiteral("Missing argument: ")+name);
        const auto type=values[name].metaType().id();
        if (type!=QMetaType::QString && type!=QMetaType::Bool && type!=QMetaType::Int
            && type!=QMetaType::UInt && type!=QMetaType::LongLong && type!=QMetaType::ULongLong
            && type!=QMetaType::Double) return fail(error,QStringLiteral("Argument must be scalar"));
    }
    QString output;
    for (qsizetype i=0; i<source.size(); ++i) {
        if (source[i]==QLatin1Char('{')) {
            if (i+1<source.size() && source[i+1]==QLatin1Char('{')) { output+=QLatin1Char('{'); ++i; }
            else { const auto end=source.indexOf(QLatin1Char('}'),i+1);
                output+=values[source.mid(i+1,end-i-1)].toString(); i=end; }
        } else if (source[i]==QLatin1Char('}') && i+1<source.size() && source[i+1]==QLatin1Char('}')) {
            output+=QLatin1Char('}'); ++i;
        } else output+=source[i];
    }
    *result=output; return true;
}
QString I18nManager::text(const QString& domain, const QString& key, const QString& fallback,
                        const QVariantMap& arguments, qint64 count) const {
    const auto snapshot=impl_->snapshot.load();
    const auto found=snapshot->domains.constFind(domain);
    QString source;
    if (found!=snapshot->domains.cend()) {
        QJsonValue value=found->current.value(domain).toObject().value(key);
        auto language=snapshot->language;
        if (value.isUndefined()) { value=found->english.value(domain).toObject().value(key); language=QStringLiteral("en-US"); }
        source=selectForm(value,language,count);
    }
    if (source.isEmpty()) {
        impl_->log(QStringLiteral("I18N_MISSING_KEY"),domain+QLatin1Char('/')+key);
        source=fallback;
        if (source.isEmpty()) source=impl_->options.showMissingKeys ? domain+QLatin1Char('/')+key : QStringLiteral("Unknown");
    }
    QString result,error;
    if (!format(source,arguments,count,&result,&error)) {
        impl_->log(QStringLiteral("I18N_FORMAT_ERROR"),domain+QLatin1Char('/')+key+QStringLiteral(": ")+error);
        if (!format(fallback,arguments,count,&result)) result=QStringLiteral("Unknown");
    }
    return result;
}
QMetaObject::Connection I18nManager::bind(QObject* context, std::function<void()> apply) {
    if (!context || !apply || !guiThread(this) || context->thread()!=thread()) return {};
    const auto connection=connect(this,&I18nManager::languageChanged,context,[apply](const QString&,quint64) { apply(); });
    apply(); return connection;
}
}
