#include "StringIDs.h"
#include <assert.h>
#include <QIcon>
#include <QMessageBox>

SharedPtr<ILanguageManager> GetLanguageManager(const SharedPtr<IValueCollection>& config)
{
	SharedPtr<ILanguageManager> result;
	assert(config);
	const auto p = VariantValue::Key("LanguageManager").Get<SharedPtr<IUnknown>>(config);
	assert(p.IsValid());

	if (!SUCCEEDED(p->QueryInterface(IID_ILanguageManager, (void**)&result.p)))
	{
		assert(false);
	}
	return result;
}

int ShowMessageBox(const SharedPtr<IValueCollection>& config, int message, void* parent /*= nullptr*/, MsgBoxType type /*= MsgBoxType::Information*/,
	std::vector<int> buttons /*= { StringID::OK }*/)
{
	QMessageBox msgBox(static_cast<QWidget*>(parent));

	msgBox.setWindowIcon(QIcon(":/ShairportQt.png"));
	msgBox.setIcon(static_cast<QMessageBox::Icon>(type));

	msgBox.setText(GetLanguageManager(config)->GetString(message).c_str());

	for (const auto& button : buttons)
	{
		QMessageBox::ButtonRole role = QMessageBox::AcceptRole;

		switch (button)
		{
		case StringID::CANCEL:
			role = QMessageBox::RejectRole;
			break;
		}
		msgBox.addButton(GetLanguageManager(config)->GetString(button).c_str(), role);
	}
	return msgBox.exec();
}
