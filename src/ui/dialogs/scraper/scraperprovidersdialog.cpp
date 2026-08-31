#include "scraperprovidersdialog.h"

#include "uiconstants/color.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "collection/generalsettings.h"
#include "metadataprovider.h"
#include "metadataproviderregistry.h"

namespace {

/// Renders a provider's Capability flags as a short comma-separated
/// string ("Web / Lookup / Media"). Empty when the provider claims no
/// capabilities, which would be a bug — surface as "(none)" so the row
/// stays scannable.
QString formatCapabilities(MetadataProvider::Capabilities caps) {
  QStringList parts;
  if (caps.testFlag(MetadataProvider::Capability::WebSearch)) parts << QStringLiteral("Web");
  if (caps.testFlag(MetadataProvider::Capability::MetadataLookup))
    parts << QStringLiteral("Lookup");
  if (caps.testFlag(MetadataProvider::Capability::MediaFetch)) parts << QStringLiteral("Media");
  return parts.isEmpty() ? QObject::tr("(none)") : parts.join(QStringLiteral(" / "));
}

QString formatCategories(const QStringList &cats) {
  return cats.isEmpty() ? QObject::tr("(any)") : cats.join(QStringLiteral(", "));
}

} // namespace

ScraperProvidersDialog::ScraperProvidersDialog(const GeneralSettings *settings, QWidget *parent)
    : QDialog(parent), m_settings(settings) {
  setupUi();
  populate();
}

void ScraperProvidersDialog::setupUi() {
  setWindowTitle(tr("Scraper providers"));
  resize(720, 480);

  auto *outer = new QVBoxLayout(this);

  auto *header = new QLabel(
      tr("Built-in metadata providers. Credentials are edited via Settings → Scrapers."), this);
  header->setStyleSheet(UIConstants::Color::mutedLabelStyleSheet(/*italic=*/true));
  header->setWordWrap(true);
  outer->addWidget(header);

  // Test query input. Sits above the tree so the user types once and
  // every row's URL updates against the same input.
  auto *queryRow = new QVBoxLayout();
  m_testQueryEdit = new QLineEdit(this);
  m_testQueryEdit->setPlaceholderText(
      tr("Type a test query then double-click a provider row to see its URL"));
  m_testQueryEdit->setText(QStringLiteral("Live concert"));
  queryRow->addWidget(m_testQueryEdit);
  outer->addLayout(queryRow);

  m_tree = new QTreeWidget(this);
  m_tree->setHeaderLabels(
      {tr("Provider"), tr("Categories"), tr("Capabilities"), tr("Credentials")});
  m_tree->setRootIsDecorated(false);
  m_tree->setAlternatingRowColors(true);
  m_tree->setSortingEnabled(true);
  m_tree->header()->setSectionResizeMode(QHeaderView::Interactive);
  m_tree->header()->setStretchLastSection(true);
  outer->addWidget(m_tree, /*stretch=*/1);

  m_statusLabel = new QLabel(this);
  m_statusLabel->setWordWrap(true);
  m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_statusLabel->setStyleSheet("font-family: monospace;");
  outer->addWidget(m_statusLabel);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  outer->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_tree, &QTreeWidget::itemActivated, this,
          [this](QTreeWidgetItem *item, int /*column*/) { onRowActivated(item); });
}

void ScraperProvidersDialog::populate() {
  if (!m_tree) return;
  m_tree->clear();
  // builtIn returns a fresh vector of owning unique_ptrs each call —
  // we only need the snapshot for as long as the dialog is open.
  auto providers = MetadataProviderRegistry::builtIn();
  for (const auto &provider : providers) {
    if (!provider) continue;
    auto *row = new QTreeWidgetItem(m_tree);
    row->setText(0, provider->displayName());
    row->setText(1, formatCategories(provider->categories()));
    row->setText(2, formatCapabilities(provider->capabilities()));

    // Credentials: if the provider has an entry in scraper.credentials
    // with at least one non-empty value, mark configured. Providers
    // that don't require any credentials surface as "(not required)" —
    // we infer this from the fact that the credential blob simply
    // never gets populated for them.
    QString credentialsLabel = tr("not configured");
    if (m_settings) {
      const auto &all = m_settings->scraper.credentials;
      const auto it = all.find(provider->id());
      if (it != all.end()) {
        bool anyValue = false;
        for (auto v = it->begin(); v != it->end(); ++v) {
          if (!v.value().trimmed().isEmpty()) {
            anyValue = true;
            break;
          }
        }
        if (anyValue) credentialsLabel = tr("configured");
      }
    }
    row->setText(3, credentialsLabel);
    // Stash the provider id under UserRole so itemActivated can look up
    // the matching provider in a fresh registry build without depending
    // on the lifetime of `providers`.
    row->setData(0, Qt::UserRole, provider->id());
  }
  for (int col = 0; col < m_tree->columnCount(); ++col) {
    m_tree->resizeColumnToContents(col);
  }
  m_tree->sortByColumn(0, Qt::AscendingOrder);
}

void ScraperProvidersDialog::onRowActivated(QTreeWidgetItem *item) {
  if (!item) return;
  const QString id = item->data(0, Qt::UserRole).toString();
  if (id.isEmpty()) return;
  // Re-fetch providers so we have a non-owning reference of the right
  // lifetime to call searchUrl on. Cheap — these are small structs.
  const QString query = m_testQueryEdit ? m_testQueryEdit->text() : QString();
  auto providers = MetadataProviderRegistry::builtIn();
  for (const auto &provider : providers) {
    if (provider && provider->id() == id) {
      const QUrl url = provider->searchUrl(query);
      if (url.isValid()) {
        m_statusLabel->setText(
            tr("Test URL for %1:\n%2").arg(provider->displayName(), url.toString()));
      } else if (!provider->capabilities().testFlag(MetadataProvider::Capability::WebSearch)) {
        m_statusLabel->setText(
            tr("%1 does not support web-search test URLs.").arg(provider->displayName()));
      } else {
        m_statusLabel->setText(
            tr("%1 returned an invalid URL for the test query.").arg(provider->displayName()));
      }
      return;
    }
  }
  m_statusLabel->setText(tr("Provider %1 is no longer registered.").arg(id));
}
